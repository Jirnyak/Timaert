// === Biome Textures — procedural macroworld ground texture system ===
//
// Layer 1 (Macroworld). Composites all biome procedural textures.
// Provides common noise primitives, biome dispatch, and neighbor-aware
// blending with shore transitions. Same approach as trees/mountains/roads.
//
// Exports BIOME_TEXTURE_GLSL — a GLSL snippet for the map fragment shader.
// Expects uniforms: u_masterTexture, u_worldSeed, u_mapSize, u_seaLevel, u_tileSize.
// Call biomeTextureOverlay(mapUV, baseColor) before road/tree/mountain overlays.

import {TUNDRA_BIOME_GLSL} from './tundra';
import {TAIGA_BIOME_GLSL} from './taiga';
import {SNOW_BIOME_GLSL} from './snow';
import {VALLEY_BIOME_GLSL} from './valley';
import {MEADOW_BIOME_GLSL} from './meadow';
import {SWAMP_BIOME_GLSL} from './swamp';
import {DESERT_BIOME_GLSL} from './desert';
import {STEPPE_BIOME_GLSL} from './steppe';
import {TROPICS_BIOME_GLSL} from './tropics';
import {WATER_BIOME_GLSL} from './water-biome';

export const BIOME_TEXTURE_GLSL = /* glsl */ `
// ── Common noise primitives (bt_ prefix) ──

float bt_hash(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float bt_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = bt_hash(i);
	float b = bt_hash(i + vec2(1.0, 0.0));
	float c = bt_hash(i + vec2(0.0, 1.0));
	float d = bt_hash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float bt_fbm(vec2 p, int oct) {
	float v = 0.0, a = 0.5, tot = 0.0;
	for (int i = 0; i < 6; i++) {
		if (i >= oct) break;
		v += a * bt_noise(p);
		tot += a;
		p *= 2.0;
		a *= 0.5;
	}
	return v / tot;
}

// ── Per-biome texture functions ──
// All receive world-space pixel coords (16× cell resolution) + seed.

${TUNDRA_BIOME_GLSL}
${TAIGA_BIOME_GLSL}
${SNOW_BIOME_GLSL}
${VALLEY_BIOME_GLSL}
${MEADOW_BIOME_GLSL}
${SWAMP_BIOME_GLSL}
${DESERT_BIOME_GLSL}
${STEPPE_BIOME_GLSL}
${TROPICS_BIOME_GLSL}
${WATER_BIOME_GLSL}

// ── Biome classification from master texture ──

int bt_biome(vec2 cell) {
	vec2 uv = fract((cell + 0.5) / u_mapSize);
	vec4 m = texture(u_masterTexture, uv);
	if (m.r < u_seaLevel) return 9;
	int row = int(clamp(m.b * 2.99, 0.0, 2.0));
	int col = int(clamp(m.g * 2.99, 0.0, 2.0));
	return row * 3 + col;
}

// ── Dispatch to biome texture function ──

vec3 bt_tex(int biome, vec2 wp, float sd) {
	if (biome == 0) return bt_tundra(wp, sd);
	if (biome == 1) return bt_taiga(wp, sd);
	if (biome == 2) return bt_snow(wp, sd);
	if (biome == 3) return bt_valley(wp, sd);
	if (biome == 4) return bt_meadow(wp, sd);
	if (biome == 5) return bt_swamp(wp, sd);
	if (biome == 6) return bt_desert(wp, sd);
	if (biome == 7) return bt_steppe(wp, sd);
	if (biome == 8) return bt_tropics(wp, sd);
	return bt_water(wp, sd);
}

// ── Per-biome base color (matches BIOMES table in biomes.ts) ──
// Used when the macroworld map canvas is disabled so biome textures still
// show meaningful colors. Multiplied with the bt_xxx modulation (~1.0).
vec3 bt_baseColor(int biome) {
	if (biome == 0) return vec3(0.50, 0.52, 0.45); // Tundra
	if (biome == 1) return vec3(0.22, 0.38, 0.28); // Taiga
	if (biome == 2) return vec3(0.90, 0.92, 0.96); // Snow
	if (biome == 3) return vec3(0.55, 0.52, 0.32); // Valley
	if (biome == 4) return vec3(0.40, 0.52, 0.28); // Meadow
	if (biome == 5) return vec3(0.28, 0.38, 0.22); // Swamp
	if (biome == 6) return vec3(0.82, 0.72, 0.48); // Desert
	if (biome == 7) return vec3(0.68, 0.60, 0.32); // Steppe
	if (biome == 8) return vec3(0.10, 0.35, 0.10); // Tropics
	return vec3(0.12, 0.22, 0.42);                  // Water
}

// ── Distance from point to line segment (same as road) ──
float bt_lineDist(vec2 p, vec2 a, vec2 b) {
	vec2 ab = b - a;
	float t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
	return length(p - (a + ab * t));
}

// ── Shore palette (single source of truth for both water & land sides) ──
vec3 bt_sandWet() { return vec3(0.55, 0.50, 0.36); }
vec3 bt_sandDry() { return vec3(0.76, 0.70, 0.52); }

// Noise wiggle along an edge of THIS cell — purely local to the cell.
// Each cell generates its own independent shore using the same algorithm
// (cell coords + edge id seed it, no boundary-shared sampling).
// Returns signed offset in pixel units (range roughly ±2.5).
float bt_edgeNoise(vec2 cell, float edgeId, float coord01, float sd) {
	float s = bt_hash(cell + sd * 0.137 + edgeId * 7.31);
	return (bt_noise(vec2(coord01 * 4.7 + s * 13.0, edgeId * 3.1 + s * 7.0)) - 0.5) * 5.0;
}

// Shore color from signed pixel distance to boundary.
//   d > 0  → inside water (crisp wet-sand band ~4 px wide)
//   d < 0  → inside land  (smooth, NOISY fade from sand to grass, no contour)
// wp is world-pixel coords (used to sample fade noise).
vec3 bt_shoreColor(vec3 baseColor, float d, float grain, vec2 wp, float sd) {
	if (d >= 0.0) {
		// Water side — keep crisp sandy ring touching the boundary.
		if (d > 4.5) return baseColor;
		vec3 sand = mix(bt_sandWet(), bt_sandDry(), smoothstep(0.0, 4.0, d)) + grain;
		float t = 1.0 - smoothstep(3.5, 4.5, d);
		return mix(baseColor, sand, t);
	}
	// Land side — wider, noisy, contour-less transition.
	float a = -d; // distance into the land (px)
	if (a > 12.0) return baseColor;
	// Two-octave noise modulates coverage so the band breaks up into
	// patches instead of a uniform stripe (no visible contour line).
	float n = bt_fbm(wp * 0.18 + sd * 0.07, 3);
	// Base coverage falls off smoothly across 12 px; noise pushes the
	// boundary in/out per pixel so there's no hard edge.
	float cov = smoothstep(12.0, 0.0, a) * (0.55 + n * 0.55);
	cov = clamp(cov, 0.0, 1.0);
	// Sand color blends from wet (near edge) to dry, then to land.
	vec3 sand = mix(bt_sandWet(), bt_sandDry(), smoothstep(0.0, 6.0, a)) + grain;
	return mix(baseColor, sand, cov);
}

// Sample temperature from the master texture for a given cell.
// Returns 0..1 where 0 = coldest, 1 = hottest.
float bt_temperature(vec2 cell) {
	vec2 uv = fract((cell + 0.5) / u_mapSize);
	return texture(u_masterTexture, uv).b;
}

// Procedural snow / ice overlay driven by continuous temperature.
// Land cells get patchy snow as temp drops below ~0.30; water cells get
// drift-ice where temp drops below ~0.18. Pure procedural — no extra data.
vec3 bt_climateOverlay(vec3 col, vec2 wp, float temp01, bool isWater, float sd) {
	if (isWater) {
		// Drift / pack ice on cold water.
		float iceMask = smoothstep(0.22, 0.05, temp01);
		if (iceMask <= 0.0) return col;
		float n = bt_fbm(wp * 0.07 + sd * 0.11, 4);
		float crack = bt_fbm(wp * 0.35, 2);
		float cov = smoothstep(0.45, 0.65, n) * iceMask;
		// Dark cracks within ice slabs
		vec3 ice = mix(vec3(0.86, 0.92, 0.97), vec3(0.70, 0.80, 0.92), crack * 0.6);
		return mix(col, ice, cov);
	}
	// Land snow patches: stronger in colder cells, broken by noise.
	float snowMask = smoothstep(0.32, 0.10, temp01);
	if (snowMask <= 0.0) return col;
	float n = bt_fbm(wp * 0.10 + sd * 0.13, 4);
	float frost = bt_fbm(wp * 0.40, 2) * 0.15;
	float cov = smoothstep(0.40, 0.70, n) * snowMask;
	vec3 snow = vec3(0.92, 0.94, 0.97) - frost;
	return mix(col, snow, cov);
}

// ── Main overlay ──

vec3 biomeTextureOverlay(vec2 mapUV, vec3 baseColor) {
	vec2 pc = mapUV * u_mapSize;
	vec2 cell = floor(pc);
	vec2 f = fract(pc);
	float sd = u_worldSeed;

	// 16×16 pixel grid (same as roads)
	vec2 p = floor(f * 16.0) + 0.5;
	// World-space pixel coords for texture functions
	vec2 wp = cell * 16.0 + p;

	int cb = bt_biome(cell);
	bool isWater = (cb == 9);

	// ── Gather 8 neighbors ──
	int nbE  = bt_biome(cell + vec2( 1, 0));
	int nbW  = bt_biome(cell + vec2(-1, 0));
	int nbN  = bt_biome(cell + vec2( 0, 1));
	int nbS  = bt_biome(cell + vec2( 0,-1));
	int nbNE = bt_biome(cell + vec2( 1, 1));
	int nbNW = bt_biome(cell + vec2(-1, 1));
	int nbSE = bt_biome(cell + vec2( 1,-1));
	int nbSW = bt_biome(cell + vec2(-1,-1));

	bool wE = nbE == 9, wW = nbW == 9, wN = nbN == 9, wS = nbS == 9;
	bool wNE = nbNE == 9, wNW = nbNW == 9, wSE = nbSE == 9, wSW = nbSW == 9;

	float grain = (bt_hash(wp + sd) - 0.5) * 0.03;

	// ── Compute signed distance to nearest water↔land boundary ──
	// Sign convention: positive = inside water, negative = inside land
	float sgn = isWater ? 1.0 : -1.0;
	// "dist" = absolute pixel distance from the noise-distorted boundary
	// to current pixel, only considering boundaries between this cell &
	// a different-domain neighbor (water vs land).
	float dist = 999.0;

	// Cardinal edges. Noise is sampled LOCALLY per cell (cell coords as
	// seed) so each tile generates its own independent wiggle using the
	// same algorithm — no mirroring across boundaries.

	// East edge
	if (isWater != (nbE == 9)) {
		float n = bt_edgeNoise(cell, 0.0, p.y / 16.0, sd);
		dist = min(dist, abs((16.0 - p.x) - n));
	}
	// West edge
	if (isWater != (nbW == 9)) {
		float n = bt_edgeNoise(cell, 1.0, p.y / 16.0, sd);
		dist = min(dist, abs(p.x - n));
	}
	// North edge
	if (isWater != (nbN == 9)) {
		float n = bt_edgeNoise(cell, 2.0, p.x / 16.0, sd);
		dist = min(dist, abs((16.0 - p.y) - n));
	}
	// South edge
	if (isWater != (nbS == 9)) {
		float n = bt_edgeNoise(cell, 3.0, p.x / 16.0, sd);
		dist = min(dist, abs(p.y - n));
	}

	// Diagonal corners — only matter when both adjacent cardinals share
	// the SAME domain as this cell (otherwise the cardinal already covers it)
	if (isWater != (nbNE == 9) && (nbN == 9) == isWater && (nbE == 9) == isWater) {
		dist = min(dist, length(p - vec2(16.0, 16.0)));
	}
	if (isWater != (nbNW == 9) && (nbN == 9) == isWater && (nbW == 9) == isWater) {
		dist = min(dist, length(p - vec2(0.0, 16.0)));
	}
	if (isWater != (nbSE == 9) && (nbS == 9) == isWater && (nbE == 9) == isWater) {
		dist = min(dist, length(p - vec2(16.0, 0.0)));
	}
	if (isWater != (nbSW == 9) && (nbS == 9) == isWater && (nbW == 9) == isWater) {
		dist = min(dist, length(p - vec2(0.0, 0.0)));
	}

	// ── Always render the biome / water texture first ──
	vec3 tex = bt_baseColor(cb) * bt_tex(cb, wp, sd);

	// Land-land biome blending at borders
	if (!isWater) {
		float blendD = 999.0;
		int blendBiome = cb;
		if (nbE != cb && nbE != 9) { float d = 16.0 - p.x; if (d < blendD) { blendD = d; blendBiome = nbE; } }
		if (nbW != cb && nbW != 9) { float d = p.x; if (d < blendD) { blendD = d; blendBiome = nbW; } }
		if (nbN != cb && nbN != 9) { float d = 16.0 - p.y; if (d < blendD) { blendD = d; blendBiome = nbN; } }
		if (nbS != cb && nbS != 9) { float d = p.y; if (d < blendD) { blendD = d; blendBiome = nbS; } }
		if (blendD < 5.0) {
			float t = smoothstep(5.0, 0.0, blendD) * 0.5;
			tex = mix(tex, bt_baseColor(blendBiome) * bt_tex(blendBiome, wp, sd), t);
		}
	}

	// ── Apply shore band on top of biome texture ──
	// Water side stays crisp (≤4.5 px); land side fades smoothly out
	// to 12 px with noise-modulated coverage (no contour line).
	float reach = isWater ? 4.5 : 12.0;
	if (dist < reach) {
		float d = sgn * dist;
		tex = bt_shoreColor(tex, d, grain, wp, sd);
	}

	// ── Procedural climate overlay (snow on cold land, ice on cold water) ──
	float temp01 = bt_temperature(cell);
	tex = bt_climateOverlay(tex, wp, temp01, isWater, sd);

	float strength = smoothstep(3.0, 10.0, u_tileSize);
	tex = mix(vec3(1.0), tex, strength);
	return baseColor * tex;
}
`;

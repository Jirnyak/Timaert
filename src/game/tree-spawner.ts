// === Tree Spawner — procedural forest placement + map overlay ===
//
// Layer 1 (Macroworld). Feature type: Tree.
// All macroworld tree logic lives here:
// • spawnTrees(): terrain data + seed → tree positions (CPU)
// • TREE_MAP_GLSL: procedural pixel-art tree overlay for map shader (GPU)
//
// Trees are rendered as a feature map overlay (same approach as mountains
// and roads). The CPU spawner stamps cells as Tree in the FeatureLayer;
// the GPU overlay draws pixel-art trees on those cells.

import type {TerrainData} from '../webgl/map-generator';

// ── Procedural tree map overlay (GLSL) ──
// Draws pixel-art trees on cells marked Tree in feature map (FeatureType=2).
// 2×2 cell footprint — trunk sits in the cell's own center, canopy naturally
// extends upward into neighbour cells. Trunks never bleed downward.
// Species determined by cell temperature (u_masterTexture.b) with per-cell
// noise variation for organic mixing within each climate band.
// 7 species: Pine (coldest) → Birch → Autumn → Oak → Cherry/Willow → Jungle (tropical).
// Expects uniforms: u_featureMap, u_masterTexture, u_worldSeed, u_mapSize.
export const TREE_MAP_GLSL = /* glsl */ `
float treeHash(float n) {
	n = fract(n * 0.1031);
	n *= n + 33.33;
	n *= n + n;
	return fract(n);
}

float treeHash2D(vec2 cell, float offset) {
	vec2 p = cell + offset;
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

// Smooth 2D value noise for organic species regions
float treeValueNoise(vec2 p, float sd) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = treeHash2D(i, sd);
	float b = treeHash2D(i + vec2(1.0, 0.0), sd);
	float c = treeHash2D(i + vec2(0.0, 1.0), sd);
	float d = treeHash2D(i + vec2(1.0, 1.0), sd);
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec4 treeDraw(vec2 cell, vec2 localUV, vec3 baseColor) {
	vec2 cellUV = (cell + 0.5) / u_mapSize;

	float featureId = texture(u_featureMap, cellUV).r * 255.0;
	if (featureId < 1.5 || featureId > 2.5) return vec4(baseColor, 0.0);

	float v1 = treeHash2D(cell, u_worldSeed + 1.0);
	float v2 = treeHash2D(cell, u_worldSeed + 2.0);

	// Temperature-driven species: read from master texture blue channel
	float temp = texture(u_masterTexture, cellUV).b;
	// Per-cell variation noise for organic mixing within temperature bands
	float vn = treeValueNoise(cell / 8.0, u_worldSeed + 77.0);

	// Temperature bands → species index
	// 0: Oak, 1: Cherry, 2: Birch, 3: Autumn, 4: Pine, 5: Willow, 6: Jungle
	int tp;
	if (temp < 0.2) {
		tp = 4; // Coldest → Pine only
	} else if (temp < 0.35) {
		tp = vn < 0.45 ? 4 : 2; // Cold → Pine / Birch
	} else if (temp < 0.5) {
		tp = vn < 0.45 ? 2 : 3; // Cool → Birch / Autumn
	} else if (temp < 0.65) {
		tp = vn < 0.4 ? 0 : vn < 0.7 ? 3 : 5; // Temperate → Oak / Autumn / Willow
	} else if (temp < 0.8) {
		tp = vn < 0.35 ? 1 : vn < 0.65 ? 0 : 5; // Warm → Cherry / Oak / Willow
	} else {
		tp = 6; // Tropical → Jungle
	}

	// 16×16 pixel grid over the 2×2 footprint
	vec2 p = floor(vec2(localUV.x, 1.0 - localUV.y) * 16.0);
	float cs = treeHash2D(cell, u_worldSeed) * 1e3;
	float ph = treeHash(cs + p.x * 17.1 + p.y * 31.7);

	float cx = 7.0 + floor((v1 - 0.5) * 2.0);

	// Palettes
	vec3 bark1, bark2, leaf1, leaf2, leaf3;
	if (tp == 0) { // Oak
		bark1 = vec3(79, 56, 41) / 255.0; bark2 = vec3(101, 67, 33) / 255.0;
		leaf1 = vec3(30, 120, 30) / 255.0; leaf2 = vec3(50, 160, 50) / 255.0; leaf3 = vec3(75, 105, 42) / 255.0;
	} else if (tp == 1) { // Cherry blossom
		bark1 = vec3(60, 40, 30) / 255.0; bark2 = vec3(85, 55, 40) / 255.0;
		leaf1 = vec3(255, 160, 180) / 255.0; leaf2 = vec3(255, 120, 165) / 255.0; leaf3 = vec3(225, 105, 145) / 255.0;
	} else if (tp == 2) { // Birch
		bark1 = vec3(195, 195, 190) / 255.0; bark2 = vec3(240, 240, 235) / 255.0;
		leaf1 = vec3(105, 195, 85) / 255.0; leaf2 = vec3(135, 215, 105) / 255.0; leaf3 = vec3(85, 165, 65) / 255.0;
	} else if (tp == 3) { // Autumn
		bark1 = vec3(70, 50, 40) / 255.0; bark2 = vec3(95, 68, 48) / 255.0;
		leaf1 = vec3(235, 125, 10) / 255.0; leaf2 = vec3(225, 65, 10) / 255.0; leaf3 = vec3(245, 200, 15) / 255.0;
	} else if (tp == 4) { // Pine
		bark1 = vec3(88, 58, 38) / 255.0; bark2 = vec3(105, 72, 52) / 255.0;
		leaf1 = vec3(12, 82, 12) / 255.0; leaf2 = vec3(32, 115, 32) / 255.0; leaf3 = vec3(18, 68, 18) / 255.0;
	} else if (tp == 5) { // Willow
		bark1 = vec3(88, 62, 48) / 255.0; bark2 = vec3(105, 72, 38) / 255.0;
		leaf1 = vec3(125, 190, 45) / 255.0; leaf2 = vec3(105, 170, 35) / 255.0; leaf3 = vec3(145, 205, 55) / 255.0;
	} else { // Jungle
		bark1 = vec3(62, 45, 30) / 255.0; bark2 = vec3(80, 55, 35) / 255.0;
		leaf1 = vec3(15, 95, 20) / 255.0; leaf2 = vec3(25, 130, 30) / 255.0; leaf3 = vec3(10, 75, 15) / 255.0;
	}

	vec3 bk = ph < 0.5 ? bark1 : bark2;
	vec3 lf = ph < 0.33 ? leaf1 : ph < 0.66 ? leaf2 : leaf3;

	vec3 col = baseColor;
	float drawn = 0.0;

	// Trunk is in the 8-14 range (cell's own center/lower half of sprite).
	// Canopy is in the 0-10 range (extends upward into neighbour cell).
	// This means trunks NEVER bleed into the cell below, only canopy overlaps above.

	if (tp == 4) {
		// ═══ PINE: triangular tiers ═══
		float trT = 10.0 - floor(v2);
		if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
		if (p.y == 15.0 && abs(p.x - cx) <= 1.0) { col = vec3(0.08, 0.12, 0.04); drawn = 0.45; }

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
		// ═══ BIRCH: white trunk, tall oval canopy ═══
		float trT = 5.0 - floor(v2);
		if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) {
			col = bk;
			if (mod(p.y + floor(v1 * 3.0), 3.0) < 1.0 && ph > 0.4) col = vec3(0.22, 0.22, 0.20);
			drawn = 1.0;
		}
		if (p.y == 15.0 && abs(p.x - cx) <= 1.0) { col = vec3(0.08, 0.12, 0.04); drawn = 0.45; }

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
		// ═══ WILLOW: wide canopy with hanging vines ═══
		float trT = 7.0;
		if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
		if (p.y == 15.0 && abs(p.x - cx) <= 2.0) { col = vec3(0.08, 0.12, 0.04); drawn = 0.45; }

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
		// ═══ JUNGLE: massive canopy, thick trunk, hanging vines ═══
		float trT = 7.0;
		// Thick trunk (2px wide)
		if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) <= 1.0) { col = bk; drawn = 1.0; }
		// Buttress roots
		if (p.y >= 13.0 && p.y <= 15.0) {
			float rootW = 2.5 - (15.0 - p.y) * 0.5;
			if (abs(p.x - cx) <= rootW && abs(p.x - cx) > 1.0) {
				col = bk * 0.85; drawn = 1.0;
			}
		}

		if (p.y == 15.0 && abs(p.x - cx) <= 3.0) { col = vec3(0.06, 0.10, 0.03); drawn = 0.45; }

		// Multi-layered canopy: two overlapping ellipses
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

		// Secondary canopy cluster offset to one side
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

		// Hanging vines
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
		// ═══ OAK (0), CHERRY (1), AUTUMN (3): round canopy ═══
		float trT = 9.0 - floor(v2 * 2.0);
		if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
		if (p.y == 15.0 && abs(p.x - cx) <= 2.0) { col = vec3(0.08, 0.12, 0.04); drawn = 0.45; }

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

	return vec4(col, drawn);
}

vec3 treeOverlay(vec2 mapUV, vec3 baseColor) {
	vec2 worldPos = mapUV * u_mapSize;
	// 2×2 footprint anchored at cell top edge: trunk stays inside the
	// tree's own cell, canopy extends upward into the neighbour above.
	vec2 baseCell = floor(worldPos - vec2(0.5, 1.0));
	vec3 col = baseColor;

	// Draw far rows (higher Y) first so closer rows paint on top (painter's algorithm)
	for (int dy = 1; dy >= 0; dy--) {
		for (int dx = 0; dx <= 1; dx++) {
			vec2 cell = mod(
				baseCell + vec2(float(dx), float(dy)), u_mapSize);

			vec2 diff = worldPos - (cell + vec2(0.5, 1.0));
			if (diff.x > u_mapSize.x * 0.5) diff.x -= u_mapSize.x;
			if (diff.x < -u_mapSize.x * 0.5) diff.x += u_mapSize.x;
			if (diff.y > u_mapSize.y * 0.5) diff.y -= u_mapSize.y;
			if (diff.y < -u_mapSize.y * 0.5) diff.y += u_mapSize.y;

			vec2 localUV = (diff + 1.0) / 2.0;
			if (localUV.x < 0.0 || localUV.x >= 1.0
				|| localUV.y < 0.0 || localUV.y >= 1.0) continue;

			vec4 t = treeDraw(cell, localUV, col);
			if (t.a > 0.5) col = t.rgb;
		}
	}

	return col;
}
`;

export type TreeSpawnConfig = {
	seed: number;
	seaLevel: number;
};

export function spawnTrees(
	tData: TerrainData,
	config: TreeSpawnConfig,
): Array<{x: number; y: number}> {
	const {seed, seaLevel} = config;
	const {width: mw, height: mh} = tData;

	// ── Smooth value noise with cubic interpolation ──

	const ihash = (x: number, y: number, sd: number): number => {
		// eslint-disable-next-line unicorn/prefer-math-trunc -- need 32-bit int coercion for hash
		let v = Math.trunc(x * 374_761 + y * 668_265 + sd * 2_246_822) | 0;
		v = Math.imul(v ^ (v >>> 13), 1_274_126_177);
		v ^= v >>> 16;
		return (v >>> 0) / 4_294_967_296;
	};

	const smoothNoise = (x: number, y: number, sd: number): number => {
		const ix = Math.floor(x);
		const iy = Math.floor(y);
		const fx = x - ix;
		const fy = y - iy;
		const sx = fx * fx * (3 - 2 * fx);
		const sy = fy * fy * (3 - 2 * fy);
		const n00 = ihash(ix, iy, sd);
		const n10 = ihash(ix + 1, iy, sd);
		const n01 = ihash(ix, iy + 1, sd);
		const n11 = ihash(ix + 1, iy + 1, sd);
		return n00 + (n10 - n00) * sx
			+ (n01 + (n11 - n01) * sx - (n00 + (n10 - n00) * sx)) * sy;
	};

	const fbm = (x: number, y: number, sd: number, octaves: number): number => {
		let value = 0;
		let amp = 1;
		let maxAmp = 0;
		let freq = 1;
		for (let i = 0; i < octaves; i++) {
			value += smoothNoise(x * freq, y * freq, sd + i * 100) * amp;
			maxAmp += amp;
			amp *= 0.5;
			freq *= 2;
		}

		return value / maxAmp;
	};

	// ── Pre-compute river exclusion mask with buffer zone ──
	const riverExclude = new Uint8Array(mw * mh);
	const RIVER_BUFFER = 2;
	for (let ri = 0; ri < mw * mh; ri++) {
		if (tData.riverData[ri] === 0) {
			continue;
		}

		const rx = ri % mw;
		const ry = (ri - rx) / mw;
		for (let dy = -RIVER_BUFFER; dy <= RIVER_BUFFER; dy++) {
			const by = ((ry + dy) % mh + mh) % mh;
			for (let dx = -RIVER_BUFFER; dx <= RIVER_BUFFER; dx++) {
				const bx = ((rx + dx) % mw + mw) % mw;
				riverExclude[by * mw + bx] = 1;
			}
		}
	}

	// ── Collect forest positions — pure noise, no road/settlement exclusion ──

	const positions: Array<{x: number; y: number}> = [];

	for (let y = 0; y < mh; y++) {
		for (let x = 0; x < mw; x++) {
			const idx = y * mw + x;

			// Hard exclusions: water, ice, rivers
			if (tData.waterData[idx] === 0) {
				continue;
			}

			if (riverExclude[idx] > 0) {
				continue;
			}

			if (tData.iceData[idx] > 0) {
				continue;
			}

			const h = tData.heightData[idx] / 255;
			// Shoreline buffer + mountain cap
			if (h < seaLevel + 0.03 || h > 0.8) {
				continue;
			}

			// Biome exclusion: desert (hot+dry) and frozen (cold+dry, cold+wet)
			const cellTemperature = tData.temperatureData[idx];
			const moist = tData.moistureData[idx];
			const tRow = Math.min(2, Math.floor(cellTemperature / 86));
			const moistCol = Math.min(2, Math.floor(moist / 86));
			// Row 0 col 0 = Tundra, row 0 col 2 = Snow, row 2 col 0 = Desert
			if ((tRow === 0 && moistCol !== 1)
				|| (tRow === 2 && moistCol === 0)) {
				continue;
			}

			// Organic noise — domain warped multi-scale FBM
			const nx = x / mw;
			const ny = y / mh;
			const warpX = fbm(nx * 8, ny * 8, seed + 100, 3);
			const warpY = fbm(nx * 8, ny * 8, seed + 200, 3);
			const wnx = nx + (warpX - 0.5) * 0.06;
			const wny = ny + (warpY - 0.5) * 0.06;

			// Regional forest patches (high freq = small patches)
			const large = fbm(wnx * 14, wny * 14, seed + 500, 4);
			// Local woodland clusters
			const med = fbm(wnx * 35, wny * 35, seed + 600, 3);
			// Fine edge detail
			const fine = fbm(wnx * 70, wny * 70, seed + 700, 2);

			const noise = large * 0.4 + med * 0.35 + fine * 0.25;

			// Probabilistic placement: noise→density via smoothstep,
			// then compare against per-cell random. Creates naturally
			// fuzzy forest edges — cells near boundary have ~50% chance.
			const t0 = 0.35;
			const t1 = 0.55;
			const clamped = Math.max(0, Math.min(1, (noise - t0) / (t1 - t0)));
			const density = clamped * clamped * (3 - 2 * clamped);
			const cellRand = ihash(x, y, seed + 999);
			if (cellRand < density) {
				positions.push({x, y});
			}
		}
	}

	return positions;
}

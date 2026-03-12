// === Tree Spawner — procedural forest placement + appearance ===
//
// Layer 1 (Macroworld). All macroworld tree logic lives here:
// • spawnTrees(): terrain data + seed → tree positions (CPU)
// • TREE_FRAG_GLSL: procedural pixel-art tree shader (GPU)
//
// ── Exemplar module pattern for procedural world features ──
//
// This file demonstrates the standard pattern: one file = one world feature,
// co-locating placement logic and GPU appearance (GLSL).
//
// To add a new procedural feature (e.g. mountains):
// 1. Create feature-spawner.ts exporting:
//    • FEATURE_FRAG_GLSL — GLSL snippet with a genFeature() function
//    • spawnFeatures()   — pure function returning positions
//    • FeatureSpawnConfig type
// 2. In renderer.ts, import and interpolate the GLSL into spriteFrag:
//    ${FEATURE_FRAG_GLSL}  and add an idx branch in main().
// 3. Done. Renderer stays generic; delete the file to remove the feature.
//
// Note: each GLSL snippet must use unique function names (genTree, genMountain…).
// If multiple features share helpers like hash(), extract a common GLSL constant.

import type {TraversabilityData} from '../webgl/map-generator';
import {xorshift32} from './rng';

// ── Procedural tree fragment shader (GLSL) ──
// Generates 6 tree types on a 16×16 virtual pixel grid.
// Expects: v_worldPos, v_spriteUV, u_worldSeed as inputs.
export const TREE_FRAG_GLSL = /* glsl */ `
float hash(float n) { return fract(sin(n) * 43758.5453123); }

vec4 genTree() {
	float tx = v_worldPos.x * 1024.0;
	float ty = v_worldPos.y * 1024.0;
	float seed = u_worldSeed + tx * 1024.0 + ty;
	int tp = int(mod(hash(u_worldSeed + floor(tx / 128.0) * 10000.0 + floor(ty / 128.0)) * 6.0, 6.0));

	// Snap to 16x16 virtual pixel grid for crisp pixel-art look
	vec2 uv = vec2(v_spriteUV.x, 1.0 - v_spriteUV.y);
	vec2 p = floor(uv * 16.0);

	float v1 = hash(seed + 1.0);
	float v2 = hash(seed + 2.0);
	float ph = hash(seed + p.x * 17.1 + p.y * 31.7); // per-pixel dither
	float cx = 7.0 + floor((v1 - 0.5) * 2.0);

	vec3 col = vec3(0.0);
	float a = 0.0;

	// ── Palettes (bark1/bark2 for trunk, leaf1/leaf2/leaf3 for canopy) ──
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
	} else { // Willow
		bark1 = vec3(88, 62, 48) / 255.0; bark2 = vec3(105, 72, 38) / 255.0;
		leaf1 = vec3(125, 190, 45) / 255.0; leaf2 = vec3(105, 170, 35) / 255.0; leaf3 = vec3(145, 205, 55) / 255.0;
	}

	vec3 bk = ph < 0.5 ? bark1 : bark2;
	vec3 lf = ph < 0.33 ? leaf1 : ph < 0.66 ? leaf2 : leaf3;

	if (tp == 4) {
		// ═══ PINE: three stacked triangular tiers ═══
		float trT = 10.0 - floor(v2);
		if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; a = 1.0; }
		if (p.y == 15.0 && abs(p.x - cx) <= 1.0) { col = vec3(0.08, 0.12, 0.04); a = 0.45; }

		float baseY = 1.0 + floor(v1 * 2.0);
		for (int i = 0; i < 3; i++) {
			float tT = baseY + float(i) * 3.0;
			float tB = tT + 3.0;
			if (p.y >= tT && p.y <= tB) {
				float frac = (p.y - tT) / 3.0;
				float halfW = 0.5 + frac * (2.2 + float(i) * 0.7);
				float eN = (hash(seed + p.y * 7.1 + float(i) * 97.0) - 0.5) * 0.7;
				if (abs(p.x - cx) <= halfW + eN) {
					vec3 lc = lf;
					if (p.y < tT + 1.0) lc *= 1.18;
					else if (p.y >= tB) lc *= 0.72;
					col = lc; a = 1.0;
				}
			}
		}

	} else if (tp == 2) {
		// ═══ BIRCH: white trunk with bark marks, tall oval canopy ═══
		float trT = 5.0 - floor(v2);
		if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) {
			col = bk;
			if (mod(p.y + floor(v1 * 3.0), 3.0) < 1.0 && ph > 0.4) col = vec3(0.22, 0.22, 0.20);
			a = 1.0;
		}
		if (p.y == 15.0 && abs(p.x - cx) <= 1.0) { col = vec3(0.08, 0.12, 0.04); a = 0.45; }

		float cY = trT - 2.5;
		float rX = 2.5 + v1 * 1.2;
		float rY = 3.5 + v2 * 1.5;
		vec2 dd = (p - vec2(cx, cY)) / vec2(rX, rY);
		float eN = (hash(seed + p.x * 11.3 + p.y * 19.7) - 0.5) * 0.25;
		if (dot(dd, dd) <= 1.0 + eN) {
			vec3 lc = lf;
			if (dd.y < -0.35) lc *= 1.18;
			else if (dd.y > 0.35) lc *= 0.78;
			col = lc; a = 1.0;
		}

	} else if (tp == 5) {
		// ═══ WILLOW: wide canopy with hanging vines ═══
		float trT = 7.0;
		if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; a = 1.0; }
		if (p.y == 15.0 && abs(p.x - cx) <= 2.0) { col = vec3(0.08, 0.12, 0.04); a = 0.45; }

		float cY = 4.5;
		float cR = 4.5 + v1;
		float d = length(p - vec2(cx, cY));
		float eN = (hash(seed + p.x * 13.3 + p.y * 23.7) - 0.5) * 1.0;
		if (d <= cR + eN) {
			vec3 lc = lf;
			if (p.y < cY - cR * 0.3) lc *= 1.15;
			else if (p.y > cY + cR * 0.15) lc *= 0.82;
			col = lc; a = 1.0;
		}

		for (int i = 0; i < 6; i++) {
			float vs = seed + float(i) * 7.3;
			if (hash(vs) > 0.55) continue;
			float vx = cx - 3.0 + float(i) * 1.2 + hash(vs + 1.0) * 0.5;
			float vineStart = cY + cR * 0.5;
			float vineLen = 2.0 + hash(vs + 2.0) * 2.5;
			if (abs(p.x - floor(vx)) < 1.0 && p.y >= vineStart && p.y < vineStart + vineLen) {
				col = lf * 0.82; a = 1.0;
			}
		}

	} else {
		// ═══ OAK (0), CHERRY (1), AUTUMN (3): round canopy ═══
		float trT = 9.0 - floor(v2 * 2.0);
		if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; a = 1.0; }
		if (p.y == 15.0 && abs(p.x - cx) <= 2.0) { col = vec3(0.08, 0.12, 0.04); a = 0.45; }

		float cR = 4.5 + v1 * 1.5;
		float cY = trT - cR + 1.5;
		float d = length(p - vec2(cx, cY));
		float eN = (hash(seed + p.x * 11.3 + p.y * 19.7) - 0.5) * 1.0;
		if (d <= cR + eN) {
			vec3 lc = lf;
			if (p.y < cY - cR * 0.3) lc *= 1.22;
			else if (p.y > cY + cR * 0.3) lc *= 0.72;
			if (d > cR + eN - 1.2) lc *= 0.88;
			col = lc; a = 1.0;
			if (tp == 1 && ph > 0.82) col = vec3(1.0, 0.96, 0.98);
		}
	}

	return vec4(col, a);
}
`;

export type TreeSpawnConfig = {
	seed: number;
	seaLevel: number;
	settlements: ReadonlyArray<{x: number; y: number}>;
	maxTrees?: number;
};

export function spawnTrees(
	tData: TraversabilityData,
	config: TreeSpawnConfig,
): Array<{x: number; y: number}> {
	const {seed, seaLevel, settlements, maxTrees = 10_000} = config;
	const {width: mw, height: mh} = tData;
	const STEP = 3;

	const rng = xorshift32(seed + 3333);

	// ── Integer hash noise for forest zone shaping ──

	const noise = (x: number, y: number, sd: number): number => {
		// eslint-disable-next-line unicorn/prefer-math-trunc -- need 32-bit int coercion for hash
		let v = Math.trunc(x * 374_761 + y * 668_265 + sd * 2_246_822) | 0;
		v = Math.imul(v ^ (v >>> 13), 1_274_126_177);
		v ^= v >>> 16;
		return (v >>> 0) / 4_294_967_296;
	};

	const fbm = (x: number, y: number, sd: number, octaves: number): number => {
		let value = 0;
		let amp = 1;
		let maxAmp = 0;
		let freq = 1;
		for (let i = 0; i < octaves; i++) {
			value += noise(x * freq, y * freq, sd + i * 100) * amp;
			maxAmp += amp;
			amp *= 0.5;
			freq *= 2;
		}

		return value / maxAmp;
	};

	// ── Collect candidate positions with density weight ──

	const candidates: Array<{x: number; y: number; w: number}> = [];

	for (let y = 0; y < mh; y += STEP) {
		for (let x = 0; x < mw; x += STEP) {
			const idx = y * mw + x;

			if (tData.data[idx] < 127) {
				continue;
			}

			if (tData.iceData[idx] > 0) {
				continue;
			}

			if (tData.roadData[idx] > 25) {
				continue;
			}

			const h = tData.heightData[idx] / 255;
			if (h - 0.05 < seaLevel || h > 0.65) {
				continue;
			}

			let nearSettlement = false;
			for (const st of settlements) {
				if (Math.abs(st.x - x) < 12 && Math.abs(st.y - y) < 12) {
					nearSettlement = true;
					break;
				}
			}

			if (nearSettlement) {
				continue;
			}

			const nx = x / mw;
			const ny = y / mh;
			const forestNoise = fbm(nx * 6, ny * 6, seed + 500, 4);
			const clusterNoise = fbm(nx * 24, ny * 24, seed + 700, 3);
			const heightFactor = 1 - Math.abs(h - 0.35) * 2.5;
			const density
				= forestNoise * 0.55
					+ clusterNoise * 0.3
					+ Math.max(0, heightFactor) * 0.15;

			if (density > 0.44) {
				const ox = Math.floor(rng() * STEP);
				const oy = Math.floor(rng() * STEP);
				candidates.push({
					x: (x + ox) % mw,
					y: (y + oy) % mh,
					w: density,
				});
			}
		}
	}

	// ── Take densest candidates ──

	candidates.sort((a, b) => b.w - a.w);
	return candidates.slice(0, maxTrees).map(c => ({x: c.x, y: c.y}));
}

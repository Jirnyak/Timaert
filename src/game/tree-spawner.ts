// === Tree Spawner — procedural forest placement ===
//
// Layer 1 (Macroworld). Pure function: terrain data + seed → tree positions.
// No UI, no GL, no game state mutation beyond returning coordinates.

import type {TraversabilityData} from '../webgl/map-generator';
import {xorshift32} from './rng';

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

	// ── FBM noise for forest zone shaping ──

	const noise = (x: number, y: number, sd: number): number => {
		const n = Math.sin(x * 12.9898 + y * 78.233 + sd * 43_758.5453) * 43_758.5453;
		return n - Math.floor(n);
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

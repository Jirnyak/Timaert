// === Difficulty Zones — universal per-cell danger altitude (Layer 1) ===
//
// Conceptually a *heightmap of danger*. Every cell carries a continuous
// `level` in [0, 1] built from three additive influences:
//
//   danger(x, y) = clamp01(
//        fbmNoise(x, y)           // Organic base relief
//      - civInfluence(x, y)       // Cities / villages / roads pull DOWN
//      + mountainInfluence(x, y)  // Mountain mass pushes UP
//   )
//
// 0.0 = absolute safe haven (city core).
// 1.0 = peak hellgate (deep mountain interior, rare wilderness pockets).
//
// The byte layer is `floor(level * 10)` clamped to 0-9 — provided so
// existing consumers (map overlay, spawn scaling, spire gating) keep their
// integer threshold API. The continuous field stays available via
// getZoneFloatAt for any future system that wants smooth gradients.
//
// Pure data, deterministic from world seed + civilization layout. Not
// serialized — regenerated on load (mirrors Politik).

import {FeatureType} from './features';
import {xorshift32} from './rng';

export const MIN_ZONE = 0;
export const MAX_ZONE = 9;
export const ZONE_COUNT = 10;

export type ZoneLayer = {
	width: number;
	height: number;
	data: Uint8Array;
	field: Float32Array;
};

export const ZONE_LABELS: readonly string[] = [
	'Safe Haven',
	'Settled',
	'Patrolled',
	'Frontier',
	'Wild',
	'Untamed',
	'Perilous',
	'Forsaken',
	'Cursed',
	'Hellgate',
];

export const ZONE_COLORS: ReadonlyArray<readonly [number, number, number]> = [
	[64, 200, 96],
	[150, 215, 80],
	[210, 220, 70],
	[245, 210, 60],
	[245, 170, 50],
	[235, 120, 45],
	[210, 70, 45],
	[170, 30, 45],
	[110, 18, 50],
	[55, 8, 30],
];

export type GenerateZonesInput = {
	width: number;
	height: number;
	seed: number;
	cities: ReadonlyArray<{x: number; y: number}>;
	villages: ReadonlyArray<{x: number; y: number}>;
	featureData: Uint8Array;
	isWater?: (x: number, y: number) => boolean;
};

// Civ pull is regional but not infinite. With these values:
//   City core (1.1) → zone 0, ~3-4 cell safe-haven radius, fades to 1-2 by ~8-10 cells.
//   Village (0.55) → zone 0-1 at center, 1-2 within a few cells.
//   Road (0.35) → zone 1-3 along the road, depending on local noise.
//   Dirt road (0.22) → zone 1-3.
const CIV_DECAY_PER_STEP = 0.06;
const CIV_CITY_STRENGTH = 1.1;
const CIV_VILLAGE_STRENGTH = 0.55;
const CIV_ROAD_STRENGTH = 0.35;
const CIV_DIRT_ROAD_STRENGTH = 0.22;

const MOUNTAIN_BASE_BOOST = 0.08;
const MOUNTAIN_DEPTH_SCALE = 0.04;
const MOUNTAIN_DEPTH_CAP = 0.45;
const WATER_BOOST = 0.05;
const FOREST_BOOST = 0.04;

const NOISE_BASE_CELLS = 96;
const NOISE_OCTAVES = 5;

export function generateZones(input: GenerateZonesInput): ZoneLayer {
	const {
		width, height, seed, cities, villages, featureData, isWater,
	} = input;
	const total = width * height;

	const civPull = new Float32Array(total);
	const queue: number[] = [];
	const seedCiv = (cx: number, cy: number, strength: number) => {
		const wx = ((cx % width) + width) % width;
		const wy = ((cy % height) + height) % height;
		const i = (wy * width) + wx;
		if (strength > civPull[i]) {
			civPull[i] = strength;
			queue.push(i);
		}
	};

	for (const c of cities) {
		seedCiv(c.x, c.y, CIV_CITY_STRENGTH);
	}

	for (const v of villages) {
		seedCiv(v.x, v.y, CIV_VILLAGE_STRENGTH);
	}

	for (let i = 0; i < total; i++) {
		const f = featureData[i] as FeatureType;
		const s = f === FeatureType.Road
			? CIV_ROAD_STRENGTH
			: (f === FeatureType.DirtRoad ? CIV_DIRT_ROAD_STRENGTH : 0);
		if (s > civPull[i]) {
			civPull[i] = s;
			queue.push(i);
		}
	}

	const diagDecay = CIV_DECAY_PER_STEP * 1.414;
	let head = 0;
	while (head < queue.length) {
		const i = queue[head++];
		const x = i % width;
		const y = Math.trunc(i / width);
		const v = civPull[i];
		for (let dy = -1; dy <= 1; dy++) {
			for (let dx = -1; dx <= 1; dx++) {
				if (dx === 0 && dy === 0) {
					continue;
				}

				const decay = (dx === 0 || dy === 0) ? CIV_DECAY_PER_STEP : diagDecay;
				const nv = v - decay;
				if (nv <= 0) {
					continue;
				}

				const nx = ((x + dx) % width + width) % width;
				const ny = ((y + dy) % height + height) % height;
				const ni = (ny * width) + nx;
				if (nv > civPull[ni] + 1e-4) {
					civPull[ni] = nv;
					queue.push(ni);
				}
			}
		}
	}

	const mtnDepth = new Float32Array(total);
	const mtnQueue: number[] = [];
	for (let i = 0; i < total; i++) {
		if ((featureData[i] as FeatureType) === FeatureType.Mountain) {
			mtnDepth[i] = Number.POSITIVE_INFINITY;
		} else {
			mtnDepth[i] = 0;
			mtnQueue.push(i);
		}
	}

	let mhead = 0;
	while (mhead < mtnQueue.length) {
		const i = mtnQueue[mhead++];
		const x = i % width;
		const y = Math.trunc(i / width);
		const d = mtnDepth[i];
		for (let dy = -1; dy <= 1; dy++) {
			for (let dx = -1; dx <= 1; dx++) {
				if (dx === 0 && dy === 0) {
					continue;
				}

				const nx = ((x + dx) % width + width) % width;
				const ny = ((y + dy) % height + height) % height;
				const ni = (ny * width) + nx;
				if ((featureData[ni] as FeatureType) !== FeatureType.Mountain) {
					continue;
				}

				const nd = d + ((dx === 0 || dy === 0) ? 1 : 1.414);
				if (nd < mtnDepth[ni] - 1e-4) {
					mtnDepth[ni] = nd;
					mtnQueue.push(ni);
				}
			}
		}
	}

	const rng = xorshift32(seed ^ 0x5A_17_E5);
	const noiseSeed = Math.trunc(rng() * 0xFF_FF_FF_FF) >>> 0;

	const hash2 = (ix: number, iy: number, salt: number): number => {
		let h = (ix * 374_761_393) ^ (iy * 668_265_263) ^ noiseSeed ^ salt;
		h = (h ^ (h >>> 13)) * 1_274_126_177;
		h ^= h >>> 16;
		return (h >>> 0) / 0xFF_FF_FF_FF;
	};

	const smoothstep = (t: number) => t * t * (3 - (2 * t));

	const valueNoise = (x: number, y: number, period: number, salt: number) => {
		const sx = x / period;
		const sy = y / period;
		const x0 = Math.floor(sx);
		const y0 = Math.floor(sy);
		const fx = smoothstep(sx - x0);
		const fy = smoothstep(sy - y0);
		const periodCellsX = Math.max(1, Math.round(width / period));
		const periodCellsY = Math.max(1, Math.round(height / period));
		const wrap = (k: number, m: number) => ((k % m) + m) % m;
		const a = hash2(wrap(x0, periodCellsX), wrap(y0, periodCellsY), salt);
		const b = hash2(wrap(x0 + 1, periodCellsX), wrap(y0, periodCellsY), salt);
		const c = hash2(wrap(x0, periodCellsX), wrap(y0 + 1, periodCellsY), salt);
		const d = hash2(wrap(x0 + 1, periodCellsX), wrap(y0 + 1, periodCellsY), salt);
		const ab = a + ((b - a) * fx);
		const cd = c + ((d - c) * fx);
		return ab + ((cd - ab) * fy);
	};

	const fbm = (x: number, y: number) => {
		let amp = 1;
		let freq = 1;
		let sum = 0;
		let norm = 0;
		for (let o = 0; o < NOISE_OCTAVES; o++) {
			const period = NOISE_BASE_CELLS / freq;
			sum += valueNoise(x, y, period, 0x91_E5 + (o * 7919)) * amp;
			norm += amp;
			amp *= 0.5;
			freq *= 2;
		}

		return sum / norm;
	};

	const field = new Float32Array(total);
	for (let y = 0; y < height; y++) {
		for (let x = 0; x < width; x++) {
			const i = (y * width) + x;
			const f = featureData[i] as FeatureType;

			let z = fbm(x, y);
			z -= civPull[i];

			if (f === FeatureType.Mountain) {
				z += MOUNTAIN_BASE_BOOST + Math.min(
					MOUNTAIN_DEPTH_CAP - MOUNTAIN_BASE_BOOST,
					mtnDepth[i] * MOUNTAIN_DEPTH_SCALE,
				);
			} else if (f === FeatureType.Tree) {
				z += FOREST_BOOST;
			}

			if (isWater?.(x, y)) {
				z += WATER_BOOST;
			}

			if (z < 0) {
				z = 0;
			} else if (z > 1) {
				z = 1;
			}

			field[i] = z;
		}
	}

	const data = new Uint8Array(total);
	for (let i = 0; i < total; i++) {
		const q = Math.floor(field[i] * ZONE_COUNT);
		data[i] = q >= ZONE_COUNT ? MAX_ZONE : q;
	}

	return {
		width, height, data, field,
	};
}

export function getZoneAt(layer: ZoneLayer, x: number, y: number): number {
	const wx = ((x % layer.width) + layer.width) % layer.width;
	const wy = ((y % layer.height) + layer.height) % layer.height;
	return layer.data[(wy * layer.width) + wx];
}

export function getZoneFloatAt(layer: ZoneLayer, x: number, y: number): number {
	const wx = ((x % layer.width) + layer.width) % layer.width;
	const wy = ((y % layer.height) + layer.height) % layer.height;
	return layer.field[(wy * layer.width) + wx];
}

export function zoneColorCss(level: number): string {
	const clamped = Math.max(MIN_ZONE, Math.min(MAX_ZONE, Math.round(level)));
	const [r, g, b] = ZONE_COLORS[clamped];
	return `rgb(${r}, ${g}, ${b})`;
}

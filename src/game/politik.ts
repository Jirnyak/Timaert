// === Politik (kingdoms, cities, roads, territory) ===
// Master settlement generator: kingdoms drive everything.
// Pipeline:
//   1. generateKingdomCities(seed, mapW, mapH) — places capitals, scatters
//      kingdom cities, builds intra-kingdom MST + inter-kingdom connections.
//   2. finalizePolitik(cities, terrain) — Lake Duchy capital snap to lake,
//      Voronoi cellOwner over land cells.

import type {City} from '../webgl/webgl-context';
import {xorshift32} from './rng';
import {torusDist, wrapCoord} from './torus';
import {type Language, createLanguage, generateName} from './language';

export type KingdomId =
	| 'empire'
	| 'old_magica'
	| 'lower_magica'
	| 'northern_magica'
	| 'lake_duchy'
	| 'timaert'
	| 'barbarian_north'
	| 'barbarian_south'
	| 'barbarian_west'
	| 'barbarian_east';

export type KingdomLineage =
	| 'empire'
	| 'magika'
	| 'timaert'
	| 'barbarians';

export type Kingdom = {
	id: KingdomId;
	name: string;
	color: string;
	rgb: [number, number, number];
	lineage: KingdomLineage;
	defIdx: number;
	capitalCityIdx: number;
	cityIdxs: number[];
	/** Procedural language used to name this kingdom's cities and villages. */
	language: Language;
};

export type KingdomDef = {
	id: KingdomId;
	name: string;
	color: string;
	lineage: KingdomLineage;
	cx: number;
	cy: number;
	region: (nx: number, ny: number) => boolean;
	minCities: number;
	maxCities: number;
	capitalRequires?: 'lake';
	priority: number;
};

// Coordinate convention: x ∈ [0,1] west→east, y ∈ [0,1] north→south.
//
// Layout:
//   • Magicas — entire north band (y<0.35), western-leaning centers.
//     Lake Duchy is a magica too.
//   • Republic of Timaert — entire east (x>0.6), NE-leaning capital.
//   • Empire of Light — equatorial band, can sprawl across most of the
//     central latitudes; western-leaning capital, by far the largest realm.
//   • Barbarian kingdoms — south + leftover edges. Names are randomized
//     at generation time (see BARBARIAN_NAME_* below).
export const KINGDOM_DEFS: KingdomDef[] = [
	{
		id: 'old_magica',
		name: 'Old Magica',
		color: '#a78bfa',
		lineage: 'magika',
		cx: 0.12, cy: 0.14,
		region: (x, y) => y < 0.35 && x < 0.4,
		minCities: 3, maxCities: 6,
		priority: 1,
	},
	{
		id: 'northern_magica',
		name: 'Northern Magica',
		color: '#7c3aed',
		lineage: 'magika',
		cx: 0.4, cy: 0.12,
		region: (x, y) => y < 0.3 && x >= 0.25 && x < 0.7,
		minCities: 8, maxCities: 14,
		priority: 2,
	},
	{
		id: 'lower_magica',
		name: 'Lower Magica',
		color: '#c4b5fd',
		lineage: 'magika',
		cx: 0.25, cy: 0.3,
		region: (x, y) => y >= 0.25 && y < 0.42 && x < 0.55,
		minCities: 5, maxCities: 10,
		priority: 3,
	},
	{
		id: 'lake_duchy',
		name: 'Lake Duchy',
		color: '#22d3ee',
		lineage: 'magika',
		cx: 0.45, cy: 0.38,
		region: (x, y) => y >= 0.3 && y < 0.5 && x >= 0.3 && x < 0.65,
		minCities: 2, maxCities: 5,
		capitalRequires: 'lake',
		priority: 0,
	},
	{
		id: 'timaert',
		name: 'Republic of Timaert',
		color: '#3b82f6',
		lineage: 'timaert',
		cx: 0.88, cy: 0.22,
		region: (x, _y) => x >= 0.6,
		minCities: 15, maxCities: 28,
		priority: 4,
	},
	{
		id: 'empire',
		name: 'Empire of Light',
		color: '#fbbf24',
		lineage: 'empire',
		cx: 0.32, cy: 0.55,
		region: (x, y) => y >= 0.4 && y <= 0.72 && x < 0.7,
		minCities: 30, maxCities: 55,
		priority: 5,
	},
	{
		id: 'barbarian_north',
		name: 'Barbarian Kingdom A',
		color: '#dc2626',
		lineage: 'barbarians',
		cx: 0.2, cy: 0.78,
		region: (x, y) => y >= 0.7 && x < 0.4,
		minCities: 1, maxCities: 5,
		priority: 6,
	},
	{
		id: 'barbarian_south',
		name: 'Barbarian Kingdom B',
		color: '#b91c1c',
		lineage: 'barbarians',
		cx: 0.5, cy: 0.88,
		region: (x, y) => y >= 0.75 && x >= 0.3 && x < 0.7,
		minCities: 1, maxCities: 6,
		priority: 6,
	},
	{
		id: 'barbarian_west',
		name: 'Barbarian Kingdom C',
		color: '#ef4444',
		lineage: 'barbarians',
		cx: 0.05, cy: 0.55,
		region: (x, y) => x < 0.15 && y >= 0.4 && y < 0.78,
		minCities: 1, maxCities: 5,
		priority: 6,
	},
	{
		id: 'barbarian_east',
		name: 'Barbarian Kingdom D',
		color: '#991b1b',
		lineage: 'barbarians',
		cx: 0.78, cy: 0.88,
		region: (x, y) => y >= 0.72 && x >= 0.55,
		minCities: 1, maxCities: 6,
		priority: 6,
	},
];

// Generic English wrappers used to dress a procedurally generated barbarian
// word into a kingdom-style title. The word itself comes from the kingdom's
// own `Language` — see `randomBarbarianName`.
const BARBARIAN_NAME_TEMPLATES: Array<(name: string) => string> = [
	n => `Kingdom of ${n}`,
	n => `Clans of ${n}`,
	n => `${n} Horde`,
	n => `${n} Tribes`,
	n => `Realm of ${n}`,
	n => `${n}land`,
];

function randomBarbarianName(
	lang: Language,
	rng: () => number,
	used: Set<string>,
): string {
	for (let attempt = 0; attempt < 50; attempt++) {
		const word = generateName(lang, rng);
		const tpl = BARBARIAN_NAME_TEMPLATES[Math.floor(rng() * BARBARIAN_NAME_TEMPLATES.length)];
		const name = tpl(word);
		if (!used.has(name)) {
			used.add(name);
			return name;
		}
	}

	return `Barbarian Realm ${used.size + 1}`;
}

export type Politik = {
	kingdoms: Record<KingdomId, Kingdom>;
	cellOwner: Uint8Array;
	width: number;
	height: number;
	colorPalette: Float32Array;
};

function hexToRgb(hex: string): [number, number, number] {
	const h = hex.replace('#', '');
	return [
		Number.parseInt(h.slice(0, 2), 16) / 255,
		Number.parseInt(h.slice(2, 4), 16) / 255,
		Number.parseInt(h.slice(4, 6), 16) / 255,
	];
}

// ── Phase 1: kingdom-driven city + road generation ─────────────────────────

export function generateKingdomCities(
	seed: number,
	mapWidth: number,
	mapHeight: number,
): {cities: City[]; politik: Politik} {
	const rng = xorshift32(seed + 4242);
	const cities: City[] = [];

	const orderedDefs = [...KINGDOM_DEFS].sort((a, b) => a.priority - b.priority);
	const kingdoms: Partial<Record<KingdomId, Kingdom>> = {};
	const usedBarbarianNames = new Set<string>();

	for (const def of orderedDefs) {
		const defIdx = KINGDOM_DEFS.indexOf(def);
		// Each kingdom gets its own procedural language. Cities and villages
		// inside the kingdom are named from this language at settlement time.
		const language = createLanguage(rng);
		const displayName = def.lineage === 'barbarians'
			? randomBarbarianName(language, rng, usedBarbarianNames)
			: def.name;

		// Place capital
		const capital = pickCapitalPosition(def, rng);
		const capitalCityIdx = cities.length;
		cities.push({
			x: capital.x, y: capital.y,
			connections: [],
			kingdomIdx: defIdx,
			isCapital: true,
		});

		// Scatter additional cities around capital, within region predicate
		const targetCount = def.minCities + Math.floor(rng() * (def.maxCities - def.minCities + 1));
		const cityIdxs = [capitalCityIdx];
		const minDist = 0.04;
		const scatterRadius = Math.max(0.08, 0.025 * Math.sqrt(targetCount));

		let attempts = 0;
		const maxAttempts = targetCount * 60;
		while (cityIdxs.length < targetCount && attempts < maxAttempts) {
			attempts++;
			const angle = rng() * Math.PI * 2;
			const r = Math.sqrt(rng()) * scatterRadius;
			const nx = wrapNorm(capital.x + Math.cos(angle) * r);
			const ny = wrapNorm(capital.y + Math.sin(angle) * r);
			if (!def.region(nx, ny)) {
				continue;
			}

			if (tooCloseToAnyCity(nx, ny, cities, minDist)) {
				continue;
			}

			cityIdxs.push(cities.length);
			cities.push({
				x: nx, y: ny,
				connections: [],
				kingdomIdx: defIdx,
				isCapital: false,
			});
		}

		kingdoms[def.id] = {
			id: def.id,
			name: displayName,
			color: def.color,
			rgb: hexToRgb(def.color),
			lineage: def.lineage,
			defIdx,
			capitalCityIdx,
			cityIdxs,
			language,
		};
	}

	// Connect kingdoms
	connectKingdomCities(cities, Object.values(kingdoms));
	bridgeAdjacentKingdoms(cities, Object.values(kingdoms));

	// Build colour palette (vec4 layout: 16 entries × 4 floats; alpha unused).
	const palette = new Float32Array(16 * 4);
	for (const def of KINGDOM_DEFS) {
		const k = kingdoms[def.id]!;
		const i = (k.defIdx + 1) * 4;
		palette[i] = k.rgb[0];
		palette[i + 1] = k.rgb[1];
		palette[i + 2] = k.rgb[2];
		palette[i + 3] = 1;
	}

	const politik: Politik = {
		kingdoms: kingdoms as Record<KingdomId, Kingdom>,
		cellOwner: new Uint8Array(0),
		width: mapWidth,
		height: mapHeight,
		colorPalette: palette,
	};
	return {cities, politik};
}

function wrapNorm(v: number): number {
	return ((v % 1) + 1) % 1;
}

function tooCloseToAnyCity(nx: number, ny: number, cities: City[], minDist: number): boolean {
	const md2 = minDist * minDist;
	for (const c of cities) {
		let dx = Math.abs(c.x - nx);
		let dy = Math.abs(c.y - ny);
		if (dx > 0.5) {
			dx = 1 - dx;
		}

		if (dy > 0.5) {
			dy = 1 - dy;
		}

		if (dx * dx + dy * dy < md2) {
			return true;
		}
	}

	return false;
}

function pickCapitalPosition(def: KingdomDef, rng: () => number): {x: number; y: number} {
	for (let i = 0; i < 200; i++) {
		const dx = (rng() + rng() - 1) * 0.08;
		const dy = (rng() + rng() - 1) * 0.08;
		const nx = wrapNorm(def.cx + dx);
		const ny = wrapNorm(def.cy + dy);
		if (def.region(nx, ny)) {
			return {x: nx, y: ny};
		}
	}

	return {x: def.cx, y: def.cy};
}

function findCheapestEdge(inTree: Set<number>, idxs: number[], cities: City[]): {from: number; to: number} | undefined {
	let bestFrom = -1;
	let bestTo = -1;
	let bestD = Infinity;
	for (const a of inTree) {
		for (const b of idxs) {
			if (inTree.has(b)) {
				continue;
			}

			const d = torusDist(cities[a].x, cities[a].y, cities[b].x, cities[b].y, 1, 1);
			if (d < bestD) {
				bestD = d;
				bestFrom = a;
				bestTo = b;
			}
		}
	}

	return bestFrom === -1 ? undefined : {from: bestFrom, to: bestTo};
}

function findClosestPair(aIdxs: number[], bIdxs: number[], cities: City[]): {a: number; b: number; dist: number} | undefined {
	let bestA = -1;
	let bestB = -1;
	let bestD = Infinity;
	for (const ai of aIdxs) {
		for (const bi of bIdxs) {
			const d = torusDist(cities[ai].x, cities[ai].y, cities[bi].x, cities[bi].y, 1, 1);
			if (d < bestD) {
				bestD = d;
				bestA = ai;
				bestB = bi;
			}
		}
	}

	return bestA === -1 ? undefined : {a: bestA, b: bestB, dist: bestD};
}

/**
 * Per-kingdom MST rooted at capital using Prim's, plus 1 extra nearest edge
 * per city for redundancy (so a single road blockade doesn't isolate cities).
 */
function connectKingdomCities(cities: City[], kingdoms: Kingdom[]): void {
	for (const k of kingdoms) {
		const idxs = k.cityIdxs;
		if (idxs.length < 2) {
			continue;
		}

		const inTree = new Set<number>([idxs[0]]);
		while (inTree.size < idxs.length) {
			const edge = findCheapestEdge(inTree, idxs, cities);
			if (!edge) {
				break;
			}

			cities[edge.from].connections.push(edge.to);
			cities[edge.to].connections.push(edge.from);
			inTree.add(edge.to);
		}

		for (const a of idxs) {
			let nearest = -1;
			let nd = Infinity;
			for (const b of idxs) {
				if (a === b) {
					continue;
				}

				if (cities[a].connections.includes(b)) {
					continue;
				}

				const d = torusDist(cities[a].x, cities[a].y, cities[b].x, cities[b].y, 1, 1);
				if (d < nd) {
					nd = d;
					nearest = b;
				}
			}

			if (nearest !== -1 && cities[a].connections.length < 4) {
				cities[a].connections.push(nearest);
				cities[nearest].connections.push(a);
			}
		}
	}
}

/**
 * For every pair of distinct kingdoms, add ONE bridge road between their two
 * closest cities (skip if absurdly distant).
 */
function bridgeAdjacentKingdoms(cities: City[], kingdoms: Kingdom[]): void {
	const seen = new Set<string>();
	for (let i = 0; i < kingdoms.length; i++) {
		for (let j = i + 1; j < kingdoms.length; j++) {
			const a = kingdoms[i];
			const b = kingdoms[j];
			const pair = findClosestPair(a.cityIdxs, b.cityIdxs, cities);
			if (!pair || pair.dist > 0.35) {
				continue;
			}

			const {a: bestA, b: bestB} = pair;
			const key = bestA < bestB ? `${bestA}-${bestB}` : `${bestB}-${bestA}`;
			if (seen.has(key)) {
				continue;
			}

			seen.add(key);
			if (!cities[bestA].connections.includes(bestB)) {
				cities[bestA].connections.push(bestB);
				cities[bestB].connections.push(bestA);
			}
		}
	}
}

// ── Phase 2: post-terrain finalization ─────────────────────────────────────

export function finalizePolitik(
	cities: City[],
	politik: Politik,
	mapWidth: number,
	mapHeight: number,
	isHabitable: (x: number, y: number) => boolean,
	isWater: (x: number, y: number) => boolean,
): void {
	for (const def of KINGDOM_DEFS) {
		if (def.capitalRequires !== 'lake') {
			continue;
		}

		const k = politik.kingdoms[def.id];
		if (!k || k.capitalCityIdx === -1) {
			continue;
		}

		const cap = cities[k.capitalCityIdx];
		const cx = Math.floor(cap.x * mapWidth);
		const cy = Math.floor(cap.y * mapHeight);
		if (countLocalWater(cx, cy, 6, isWater, mapWidth, mapHeight) >= 4) {
			continue;
		}

		const target = findLakeAdjacentCell(def.region, isHabitable, isWater, mapWidth, mapHeight, cx, cy);
		if (target) {
			cap.x = target.x / mapWidth;
			cap.y = target.y / mapHeight;
		}
	}

	politik.cellOwner = buildCellOwnership(cities, mapWidth, mapHeight, isHabitable);
}

function countLocalWater(
	cx: number, cy: number, radius: number,
	isWater: (x: number, y: number) => boolean,
	w: number, h: number,
): number {
	let count = 0;
	for (let dy = -radius; dy <= radius; dy++) {
		for (let dx = -radius; dx <= radius; dx++) {
			if (isWater(wrapCoord(cx + dx, w), wrapCoord(cy + dy, h))) {
				count++;
			}
		}
	}

	return count;
}

function findLakeAdjacentCell(
	region: (nx: number, ny: number) => boolean,
	isHabitable: (x: number, y: number) => boolean,
	isWater: (x: number, y: number) => boolean,
	w: number, h: number,
	cx: number, cy: number,
): {x: number; y: number} | undefined {
	for (let r = 1; r < 80; r++) {
		for (let dy = -r; dy <= r; dy++) {
			for (let dx = -r; dx <= r; dx++) {
				if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) {
					continue;
				}

				const x = wrapCoord(cx + dx, w);
				const y = wrapCoord(cy + dy, h);
				if (!region(x / w, y / h)) {
					continue;
				}

				if (!isHabitable(x, y)) {
					continue;
				}

				if (countLocalWater(x, y, 8, isWater, w, h) >= 4) {
					return {x, y};
				}
			}
		}
	}

	return undefined;
}

/**
 * Multi-source BFS over habitable cells. Each city is a seed; the first
 * wave to reach a cell claims it. Waves do NOT cross water/ice, so
 * territories are naturally bounded by coastlines and never "jump" the
 * sea. Result: organic, connected, terrain-aware kingdom shapes.
 */
function buildCellOwnership(
	cities: City[],
	w: number, h: number,
	isHabitable: (x: number, y: number) => boolean,
): Uint8Array {
	const n = w * h;
	const owner = new Uint8Array(n);

	// Ring buffer queue: 32-bit packed (cellIdx). Allocate once.
	const queue = new Int32Array(n);
	let head = 0;
	let tail = 0;

	// Seed every city's cell.
	for (const c of cities) {
		if (c.kingdomIdx < 0) {
			continue;
		}

		const cx = wrapCoord(Math.floor(c.x * w), w);
		const cy = wrapCoord(Math.floor(c.y * h), h);
		if (!isHabitable(cx, cy)) {
			continue;
		}

		const idx = cy * w + cx;
		if (owner[idx] !== 0) {
			continue;
		}

		owner[idx] = c.kingdomIdx + 1;
		queue[tail++] = idx;
	}

	// 4-neighbour BFS over habitable cells (toroidal wrap).
	while (head < tail) {
		const idx = queue[head++];
		const y = Math.trunc(idx / w);
		const x = idx - (y * w);
		const kid = owner[idx];

		const nx = (x + 1) % w;
		const px = (x + w - 1) % w;
		const ny = (y + 1) % h;
		const py = (y + h - 1) % h;

		const nbrs: number[] = [
			ny * w + x,
			py * w + x,
			y * w + nx,
			y * w + px,
		];

		for (const ni of nbrs) {
			if (owner[ni] !== 0) {
				continue;
			}

			const nyy = Math.trunc(ni / w);
			const nxx = ni - (nyy * w);
			if (!isHabitable(nxx, nyy)) {
				continue;
			}

			owner[ni] = kid;
			queue[tail++] = ni;
		}
	}

	return owner;
}


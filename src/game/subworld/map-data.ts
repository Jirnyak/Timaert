// === Subworld map data — shared types, tile constants, helpers ===

export const TILE_EMPTY = 0;
export const TILE_ROAD = 1;
export const TILE_HOUSE = 2;
export const TILE_WALL = 3;
export const TILE_FIELD = 4;
export const TILE_GRASS = 5;
export const TILE_SQUARE = 6;
export const TILE_TREE_DECOR = 7;

/**
 * Cell terrain — determines the wilderness generator when no landmark is present.
 * Add new terrain types here; create a matching `<terrain>.ts` generator.
 */
export type CellTerrain = 'forest' | 'grassland';

/**
 * Cell landmark — determines the landmark generator (overrides terrain).
 * Add new landmark types here; create a matching `<landmark>.ts` generator.
 */
export type CellLandmark = 'city' | 'village' | 'ruin';

/**
 * Subworld mode — union of all generator keys.
 * Each key maps to exactly one generator function in the registry.
 */
export type SubworldMode = CellTerrain | CellLandmark;

export type Point = {x: number; y: number};

export type StreetNode = {
	x: number;
	y: number;
	isMain: boolean;
};

export type StreetEdge = {
	p1: number;
	p2: number;
};

export type House = {
	x: number;
	y: number;
	w: number;
	h: number;
	rotation: number;
};

export type FieldPlot = {
	x: number;
	y: number;
	w: number;
	h: number;
	rotation: number;
};

export type WallRing = {
	nodes: Point[];
	avgRadius: number;
	centerX: number;
	centerY: number;
	gateAngles: number[];
	gateHalfArc: number;
};

/** Raw generation output — produced by any map generator. */
export type MapData = {
	grid: Uint8Array;
	width: number;
	height: number;
	spawnX: number;
	spawnY: number;
	seed: number;
	mode: SubworldMode;
	houses: House[];
	walls: WallRing[];
	fieldPlots: FieldPlot[];
	mainRoadPaths: Point[][];
	streetNodes: StreetNode[];
	streetEdges: StreetEdge[];
};

/** Consumer-facing result with rendered visual + traversability. */
export type SubworldMapData = {
	visual: HTMLCanvasElement;
	grid: Uint8Array;
	tileGrid: Uint8Array;
	width: number;
	height: number;
	spawnX: number;
	spawnY: number;
};

// ── Seeded PRNG ─────────────────────────────────────────────────

export class MapRng {
	private state: number;
	readonly worldSeed: number;

	constructor(seed: number) {
		// eslint-disable-next-line unicorn/prefer-math-trunc -- 32-bit coercion; || 1 guards zero
		this.state = (seed | 0) || 1;
		this.worldSeed = seed;
	}

	random(): number {
		let s = this.state;
		s ^= s << 13;
		s ^= s >>> 17;
		s ^= s << 5;
		this.state = s;
		return (s >>> 0) / 4_294_967_296;
	}

	randInt(min: number, max: number): number {
		return Math.floor(this.random() * (max - min + 1)) + min;
	}

	randFloat(min: number, max: number): number {
		return (this.random() * (max - min)) + min;
	}
}

// ── Geometry helpers ────────────────────────────────────────────

export function angularDistance(a: number, b: number): number {
	const twoPi = Math.PI * 2;
	let d = Math.abs(a - b) % twoPi;
	if (d > Math.PI) {
		d = twoPi - d;
	}

	return d;
}

export function isGateAngle(wall: WallRing, angle: number): boolean {
	for (const gateAngle of wall.gateAngles) {
		if (angularDistance(gateAngle, angle) <= wall.gateHalfArc) {
			return true;
		}
	}

	return false;
}

// ── Tile queries (operate on raw tile grid) ─────────────────────

export function findTileNear(
	grid: Uint8Array, width: number, height: number,
	targetX: number, targetY: number,
	tileType: number, searchRadius: number,
): Point | undefined {
	for (let r = 0; r < searchRadius; r++) {
		for (let dy = -r; dy <= r; dy++) {
			for (let dx = -r; dx <= r; dx++) {
				if (Math.abs(dx) !== r && Math.abs(dy) !== r) {
					continue;
				}

				const gx = Math.floor(targetX) + dx;
				const gy = Math.floor(targetY) + dy;
				if (gx >= 0 && gx < width && gy >= 0 && gy < height
					&& grid[gy * width + gx] === tileType) {
					return {x: gx + 0.5, y: gy + 0.5};
				}
			}
		}
	}

	return undefined;
}

export function findRoadNearHouses(
	grid: Uint8Array, width: number, height: number,
	rng: () => number,
): Point | undefined {
	for (let attempt = 0; attempt < 200; attempt++) {
		const gx = Math.floor(rng() * width);
		const gy = Math.floor(rng() * height);
		if (grid[gy * width + gx] !== TILE_ROAD) {
			continue;
		}

		let nearHouse = false;
		for (let dy = -3; dy <= 3 && !nearHouse; dy++) {
			for (let dx = -3; dx <= 3 && !nearHouse; dx++) {
				const nx = gx + dx;
				const ny = gy + dy;
				if (nx >= 0 && nx < width && ny >= 0 && ny < height
					&& grid[ny * width + nx] === TILE_HOUSE) {
					nearHouse = true;
				}
			}
		}

		if (nearHouse) {
			return {x: gx + 0.5, y: gy + 0.5};
		}
	}

	return undefined;
}

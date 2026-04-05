// === Subworld map data — shared types, tile constants, helpers ===
//
// Might & Magic style 3D subworld: 1024×1024 plane with heightmap,
// structures as 2D shapes with height, raycaster-rendered.

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

// ── 3D Structures (Might & Magic style) ─────────────────────────

/** Shape of a structure footprint on the 2D plane. */
export type StructureShape = 'rect' | 'circle';

/**
 * Structure state — tracks whether a structure is active, abandoned,
 * or withered (for natural features like trees).
 */
export type StructureState = 'active' | 'abandoned' | 'withered';

/**
 * A structure in the subworld — 2D footprint with height for 3D rendering.
 *
 * Rectangles render as boxes; circles render as cylinders.
 * Each structure has two texture identifiers:
 *   roofTexture — top/bottom (horizontal) face
 *   wallTexture — side (vertical) faces
 *
 * Sprites (trees, props) are point-like billboards — shape='rect' with
 * w=h=0, rendered as camera-facing quads instead of geometry.
 */
export type Structure = {
	/** Unique id within this subworld (for save/diff). */
	id: number;
	/** Type tag for regeneration diffing (e.g. 'house', 'wall', 'tower', 'tree'). */
	tag: string;
	/** 2D footprint shape. */
	shape: StructureShape;
	/** Center X on the 1024×1024 plane. */
	x: number;
	/** Center Y on the 1024×1024 plane. */
	y: number;
	/** Width (X-axis extent for rect, diameter for circle). */
	w: number;
	/** Length (Y-axis extent for rect, same as w for circle). */
	l: number;
	/** Height above terrain for 3D extrusion. */
	height: number;
	/** Rotation in radians (only meaningful for rect). */
	rotation: number;
	/** Roof / floor texture id. */
	roofTexture: string;
	/** Wall texture id. */
	wallTexture: string;
	/** Whether this structure blocks movement. */
	solid: boolean;
	/** State for regeneration (abandoned houses, withered trees). */
	state: StructureState;
	/** Whether this is a billboard sprite (tree, decorative). */
	sprite: boolean;
	/** Sprite color (for procedural sprites like trees). */
	spriteColor?: string;
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
	/** Heightmap — terrain elevation per cell (0.0–1.0 range, scaled to world units). */
	heightmap: Float32Array;
	/** 3D structures built from houses/walls/trees. */
	structures: Structure[];
};

/**
 * Serialisable subworld snapshot — saved when the player leaves.
 * Heightmap is stored as Uint16Array (0–65535 → 0.0–1.0) for compactness.
 */
export type SavedSubworldData = {
	seed: number;
	mode: SubworldMode;
	width: number;
	height: number;
	/** Quantised heightmap (Uint16). */
	heightmap: Uint16Array;
	/** All structures at time of save. */
	structures: Structure[];
	/** Next structure id counter. */
	nextStructureId: number;
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
	/** Heightmap for 3D terrain. */
	heightmap: Float32Array;
	/** Structures for 3D rendering. */
	structures: Structure[];
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

/** Pre-collect all road tiles adjacent to houses — O(n) full scan, called once. */
export function collectRoadNearHouses(grid: Uint8Array, width: number, height: number): Point[] {
	const result: Point[] = [];
	for (let gy = 0; gy < height; gy++) {
		for (let gx = 0; gx < width; gx++) {
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
				result.push({x: gx + 0.5, y: gy + 0.5});
			}
		}
	}

	return result;
}

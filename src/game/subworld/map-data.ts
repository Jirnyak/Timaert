// === Subworld map data — shared types, tile constants, helpers ===
//
// Might & Magic style 3D subworld: 1024×1024 plane with heightmap,
// structures as 2D shapes with height, raycaster-rendered.
//
// Seamless 9-cell architecture: the player's current macroworld cell
// plus 8 neighbours are generated simultaneously. Movement across
// cell boundaries triggers an incremental shift — only new neighbours
// are generated while stale ones are saved and freed.

import {Biome} from '../biomes';

export {Biome}; // eslint-disable-line unicorn/prefer-export-from -- Need local Biome for type annotations

export const TILE_EMPTY = 0;
export const TILE_ROAD = 1;
export const TILE_HOUSE = 2;
export const TILE_WALL = 3;
export const TILE_FIELD = 4;
export const TILE_GRASS = 5; // Meadow ground — kept for legacy/default
export const TILE_SQUARE = 6;
export const TILE_TREE_DECOR = 7;

// ── Biome ground tiles (8-16) ───────────────────────────────────
// Each biome has a unique ground tile that maps to a procedural texture.
// TILE_GRASS (5) serves as Meadow ground for backward compatibility.

export const TILE_TUNDRA = 8;
export const TILE_TAIGA = 9;
export const TILE_SNOW = 10;
export const TILE_VALLEY = 11;
// Meadow = TILE_GRASS (5)
export const TILE_SWAMP = 12;
export const TILE_DESERT = 13;
export const TILE_STEPPE = 14;
export const TILE_TROPICS = 15;
export const TILE_WATER = 16;
export const TILE_SHORE = 17;
export const TILE_ROCK = 18;

/** Map a Biome enum to its ground tile constant. */
const BIOME_GROUND_TILES: readonly number[] = [
	TILE_TUNDRA, // 0 = Tundra
	TILE_TAIGA, // 1 = Taiga
	TILE_SNOW, // 2 = Snow
	TILE_VALLEY, // 3 = Valley
	TILE_GRASS, // 4 = Meadow
	TILE_SWAMP, // 5 = Swamp
	TILE_DESERT, // 6 = Desert
	TILE_STEPPE, // 7 = Steppe
	TILE_TROPICS, // 8 = Tropics
	TILE_WATER, // 9 = Water
];

/** Get the ground tile for a biome. */
export function biomeGroundTile(biome: Biome): number {
	return BIOME_GROUND_TILES[biome] ?? TILE_GRASS;
}

/**
 * Cell terrain — determines the wilderness generator when no landmark is present.
 * Add new terrain types here; create a matching `<terrain>.ts` generator.
 */
export type CellTerrain = 'forest' | 'grassland' | 'mountain' | 'road' | 'water' | 'swamp';

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
	/** Biome of the center cell (determines ground texture). */
	biome: Biome;
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
	/** Universal water level (0.0–1.0). Terrain below this shows water. */
	waterLevel: number;
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
	visual: HTMLCanvasElement | ImageBitmap;
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

// ── Seamless 9-cell context ─────────────────────────────────────

/**
 * Compass direction index — clockwise from north.
 * Matches the 8 neighbour slots in NeighborGrid.
 */
export enum Dir {
	N = 0, NE = 1, E = 2, SE = 3,
	S = 4, SW = 5, W = 6, NW = 7,
}

/** Offsets for each Dir (dx, dy). Y increases southward. */
export const DIR_OFFSETS: ReadonlyArray<readonly [number, number]> = [
	[0, -1], // N
	[1, -1], // NE
	[1, 0], // E
	[1, 1], // SE
	[0, 1], // S
	[-1, 1], // SW
	[-1, 0], // W
	[-1, -1], // NW
];

/** Return the Dir from one cell toward a neighbour, or undefined if same cell. */
export function dirFromOffset(dx: number, dy: number): Dir | undefined {
	for (let d = 0; d < 8; d++) {
		if (DIR_OFFSETS[d][0] === dx && DIR_OFFSETS[d][1] === dy) {
			return d as Dir;
		}
	}

	return undefined;
}

/** Opposite direction. */
export function oppositeDir(d: Dir): Dir {
	return ((d + 4) % 8) as Dir;
}

/**
 * Macroworld cell feature id — mirrors FeatureType from features.ts
 * but duplicated here to keep subworld self-contained (no upward import).
 */
export enum CellFeature {
	None = 0,
	Road = 1,
	Tree = 2,
	Mountain = 3,
	DirtRoad = 4,
}

/**
 * Compact description of a single macroworld cell, provided by the
 * macroworld layer when the player enters the subworld.
 */
export type CellContext = {
	/** Macroworld cell X. */
	cellX: number;
	/** Macroworld cell Y. */
	cellY: number;
	/** Primary feature on this cell. */
	feature: CellFeature;
	/** Land biome determined by temperature × moisture. */
	biome: Biome;
	/** Landmark type, if any. */
	landmark: CellLandmark | undefined;
	/** Settlement/village reference for landmarks (population, name, id). */
	landmarkParam: number;
	/** Macroworld height at this cell (0–1). */
	macroHeight: number;
	/** Deterministic seed for this cell. */
	seed: number;
};

/**
 * 3×3 grid of CellContext centred on the player's current cell.
 * Index order: [NW, N, NE, W, center, E, SW, S, SE] = row-major
 * (row 0 = north, row 2 = south).
 */
export type NeighborGrid = {
	/** 9 cells, row-major (idx = (row * 3) + col), center = 4. */
	cells: readonly [
		CellContext, CellContext, CellContext,
		CellContext, CellContext, CellContext,
		CellContext, CellContext, CellContext,
	];
	/** Convenience: centre cell (same ref as cells[4]). */
	center: CellContext;
	/** Macroworld sea level (0..1). Universal water plane height. */
	seaLevel: number;
};

/** Build a NeighborGrid from a function that resolves macroworld cells. */
export function buildNeighborGrid(
	centerX: number,
	centerY: number,
	resolve: (cx: number, cy: number) => CellContext,
	seaLevel = 0.4,
): NeighborGrid {
	const cells = [] as CellContext[];
	for (let row = 0; row < 3; row++) {
		for (let col = 0; col < 3; col++) {
			cells.push(resolve(centerX + col - 1, centerY + row - 1));
		}
	}

	return {
		cells: cells as unknown as NeighborGrid['cells'],
		center: cells[4],
		seaLevel,
	};
}

/** Index into NeighborGrid.cells from a Dir (relative to center). */
export function neighborIdx(d: Dir): number {
	const [dx, dy] = DIR_OFFSETS[d];
	return (dy + 1) * 3 + (dx + 1);
}

/**
 * Get the set of Dirs toward neighbours that have a road feature.
 * Used by generators to align roads/gates to macroworld connectivity.
 */
export function roadDirections(grid: NeighborGrid): Dir[] {
	const dirs: Dir[] = [];
	for (let d = 0; d < 8; d++) {
		const idx = neighborIdx(d as Dir);
		const f = grid.cells[idx].feature;
		if (f === CellFeature.Road || f === CellFeature.DirtRoad) {
			dirs.push(d as Dir);
		}
	}

	return dirs;
}

// ── Edge anchors — deterministic cross-cell stitching ───────────

/**
 * Stitchable feature type. Extend this union when adding new linear
 * features that must be continuous across cell boundaries.
 */
export type StitchableFeature = 'road';

/**
 * A deterministic anchor point on a cell edge where a stitchable feature
 * crosses into the neighbouring cell. Both cells sharing an edge compute
 * the same position, guaranteeing seamless continuity.
 */
export type EdgeAnchor = {
	dir: Dir;
	/** Local tile X on this cell's grid. */
	x: number;
	/** Local tile Y on this cell's grid. */
	y: number;
	feature: StitchableFeature;
};

/**
 * Return true when the centre cell needs a connecting road/path toward
 * neighbour `b` — both must be road-compatible, and at least one must
 * have an actual Road/DirtRoad macroworld feature.
 */
function needsRoadStitch(a: CellContext, b: CellContext): boolean {
	const isRoad = (c: CellContext) =>
		c.feature === CellFeature.Road
		|| c.feature === CellFeature.DirtRoad;
	const isRoadLike = (c: CellContext) =>
		isRoad(c) || c.landmark !== undefined;
	return isRoadLike(a) && isRoadLike(b) && (isRoad(a) || isRoad(b));
}

/**
 * Symmetric (order-independent) seed for a shared edge between two cells.
 * Using cell coordinates makes it immune to seed collisions.
 */
function symmetricEdgeSeed(a: CellContext, b: CellContext): number {
	const ka = a.cellX * 100_003 + a.cellY;
	const kb = b.cellX * 100_003 + b.cellY;
	const lo = Math.min(ka, kb);
	const hi = Math.max(ka, kb);
	return Math.trunc((lo * 374_761_393) ^ (hi * 1_274_126_177)) || 1;
}

/**
 * Compute deterministic edge anchors for all directions that require
 * stitching. Both adjacent cells produce matching anchor positions on
 * their shared edge.
 *
 * @param neighbors  3×3 NeighborGrid centred on the current cell.
 * @param cellSize   Tile count per cell dimension (typically 1024).
 * @returns Array of EdgeAnchors in local coordinates of the centre cell.
 */
export function computeEdgeAnchors(neighbors: NeighborGrid, cellSize: number): EdgeAnchor[] {
	const anchors: EdgeAnchor[] = [];
	const {center} = neighbors;

	for (let d = 0; d < 8; d++) {
		const dir = d as Dir;
		const idx = neighborIdx(dir);
		const neighbor = neighbors.cells[idx];

		if (!needsRoadStitch(center, neighbor)) {
			continue;
		}

		const edgeSeed = symmetricEdgeSeed(center, neighbor);
		const rng = new MapRng(edgeSeed);
		// Position along the shared edge: 35–65% to stay away from corners
		const edgePos = Math.floor(cellSize * (0.35 + rng.random() * 0.3));

		const [dx, dy] = DIR_OFFSETS[dir];
		let ax: number;
		let ay: number;

		if (dx === 0) {
			// N or S — horizontal edge, position varies along X
			ax = edgePos;
			ay = dy < 0 ? 1 : cellSize - 2;
		} else if (dy === 0) {
			// E or W — vertical edge, position varies along Y
			ax = dx > 0 ? cellSize - 2 : 1;
			ay = edgePos;
		} else {
			// Diagonal — anchor right at the shared corner so both cells agree
			ax = dx > 0 ? cellSize - 2 : 1;
			ay = dy > 0 ? cellSize - 2 : 1;
		}

		anchors.push({
			dir, x: ax, y: ay, feature: 'road',
		});
	}

	return anchors;
}

/**
 * Look up an edge anchor for a specific direction.
 * Returns undefined when no stitching is needed in that direction.
 */
export function anchorForDir(anchors: EdgeAnchor[], dir: Dir): EdgeAnchor | undefined {
	return anchors.find(a => a.dir === dir);
}

/**
 * Compute a blended macro-heightmap for the 3×3 cell area.
 * Returns a Float32Array of size (subSize × 3)² where subSize is
 * a single cell's subworld tile count.
 * Used as a coarse base elevation that local generators add detail onto.
 */
export function blendedMacroHeightmap(grid: NeighborGrid, subSize: number): Float32Array {
	const fullSize = subSize * 3;
	const hm = new Float32Array(fullSize * fullSize);
	for (let gy = 0; gy < fullSize; gy++) {
		for (let gx = 0; gx < fullSize; gx++) {
			// Bilinear interpolation between the 4 nearest cell centers
			const fx = gx / subSize; // 0..3 fractional cell coords
			const fy = gy / subSize;
			// Cell indices (clamped)
			const cx = Math.min(2, Math.max(0, Math.floor(fx - 0.5)));
			const cy = Math.min(2, Math.max(0, Math.floor(fy - 0.5)));
			const cx2 = Math.min(2, cx + 1);
			const cy2 = Math.min(2, cy + 1);
			const tx = (fx - 0.5) - cx;
			const ty = (fy - 0.5) - cy;
			const sx = Math.max(0, Math.min(1, tx));
			const sy = Math.max(0, Math.min(1, ty));
			const h00 = grid.cells[cy * 3 + cx].macroHeight;
			const h10 = grid.cells[cy * 3 + cx2].macroHeight;
			const h01 = grid.cells[cy2 * 3 + cx].macroHeight;
			const h11 = grid.cells[cy2 * 3 + cx2].macroHeight;
			hm[gy * fullSize + gx]
				= h00 * (1 - sx) * (1 - sy)
					+ h10 * sx * (1 - sy)
					+ h01 * (1 - sx) * sy
					+ h11 * sx * sy;
		}
	}

	return hm;
}

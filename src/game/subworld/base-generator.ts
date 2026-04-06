// === Base map generator — shared grid + heightmap foundation ===

import {
	TILE_EMPTY, TILE_ROAD, TILE_HOUSE, TILE_WALL,
	TILE_TREE_DECOR,
	type Point, type StreetNode, type StreetEdge,
	type House, type FieldPlot, type WallRing, type MapData,
	type SubworldMode, type Structure, type StructureState,
	type NeighborGrid,
	MapRng, angularDistance, isGateAngle, blendedMacroHeightmap,
} from './map-data';
import {TREE_TYPES} from './textures';

/** Abstract base — subclassed by CityGenerator and NatureGenerator. */
export abstract class BaseMapGenerator {
	readonly width: number;
	readonly height: number;
	readonly mode: SubworldMode;
	readonly streetWidth: number;
	readonly rng: MapRng;
	grid: Uint8Array;

	readonly streetNodes: StreetNode[] = [];
	readonly streetEdges: StreetEdge[] = [];
	readonly houses: House[] = [];
	readonly walls: WallRing[] = [];
	readonly fieldPlots: FieldPlot[] = [];
	readonly mainRoadPaths: Point[][] = [];
	readonly structures: Structure[] = [];

	readonly centerX: number;
	readonly centerY: number;

	/** Optional macroworld neighbor grid for height blending. */
	neighborGrid?: NeighborGrid;

	private nextStructureId = 0;

	constructor(readonly seed: number, width: number, height: number, mode: SubworldMode, streetWidth = 1) {
		this.rng = new MapRng(seed);
		this.width = width;
		this.height = height;
		this.mode = mode;
		this.streetWidth = streetWidth;
		this.grid = new Uint8Array(width * height);
		this.centerX = Math.floor(width / 2) + (this.rng.random() > 0.5 ? this.rng.randInt(-10, 10) : 0);
		this.centerY = Math.floor(height / 2) + (this.rng.random() > 0.5 ? this.rng.randInt(-10, 10) : 0);
	}

	/** Generate the tile map. Must be implemented by subclasses. */
	abstract generateTiles(value: number): void;

	/** Produce a MapData snapshot — call after generateTiles. */
	toMapData(): MapData {
		const heightmap = this.generateHeightmap();
		// Derive structures from houses/walls if not explicitly built
		if (this.structures.length === 0) {
			this.buildStructuresFromTiles();
		}

		return {
			grid: this.grid,
			width: this.width,
			height: this.height,
			spawnX: this.centerX,
			spawnY: this.centerY,
			seed: this.rng.worldSeed,
			mode: this.mode,
			houses: this.houses,
			walls: this.walls,
			fieldPlots: this.fieldPlots,
			mainRoadPaths: this.mainRoadPaths,
			streetNodes: this.streetNodes,
			streetEdges: this.streetEdges,
			heightmap,
			structures: this.structures,
		};
	}

	// ── Heightmap generation ─────────────────────────────────────

	/**
	 * Generate terrain heightmap using multi-octave coherent noise.
	 * Roads and squares are flattened. Hills roll gently.
	 */
	generateHeightmap(): Float32Array {
		const {width, height} = this;
		const hm = new Float32Array(width * height);

		// Macro-blended base: if a NeighborGrid is provided, use interpolated
		// macroworld heights as the coarse terrain layer. The centre cell
		// occupies the middle third of the 3×3 blended map.
		let macroBase: Float32Array | undefined;
		if (this.neighborGrid) {
			const full = blendedMacroHeightmap(this.neighborGrid, width);
			// Extract the centre cell slice (row 1, col 1 of the 3×3)
			macroBase = new Float32Array(width * height);
			const fullW = width * 3;
			for (let y = 0; y < height; y++) {
				for (let x = 0; x < width; x++) {
					macroBase[y * width + x] = full[(y + height) * fullW + (x + width)];
				}
			}
		}

		// Multi-octave noise for natural terrain
		for (let y = 0; y < height; y++) {
			for (let x = 0; x < width; x++) {
				let h = 0;
				h += this.smoothTerrainNoise(x * 0.008, y * 0.008) * 0.5;
				h += this.smoothTerrainNoise(x * 0.02, y * 0.02) * 0.25;
				h += this.smoothTerrainNoise(x * 0.06, y * 0.06) * 0.125;
				// Normalize to 0–1 range (noise returns 0–1 per octave)
				h = Math.max(0, Math.min(1, h / 0.875));
				// Blend with macro base: 70% macro + 30% local detail
				if (macroBase) {
					h = macroBase[y * width + x] * 0.7 + h * 0.3;
				}

				hm[y * width + x] = h;
			}
		}

		// Flatten roads and squares
		for (let y = 0; y < height; y++) {
			for (let x = 0; x < width; x++) {
				const tile = this.grid[y * width + x];
				if (tile === TILE_ROAD || tile === 6) { // TILE_SQUARE = 6
					// Flatten to local average
					const i = y * width + x;
					hm[i] = this.localAvgHeight(hm, x, y, 3);
				}
			}
		}

		// Smooth pass for road/square transitions
		for (let pass = 0; pass < 2; pass++) {
			for (let y = 1; y < height - 1; y++) {
				for (let x = 1; x < width - 1; x++) {
					const tile = this.grid[y * width + x];
					if (tile === TILE_ROAD || tile === 6) {
						const i = y * width + x;
						hm[i] = (hm[i] * 2 + hm[i - 1] + hm[i + 1]
							+ hm[i - width] + hm[i + width]) / 6;
					}
				}
			}
		}

		return hm;
	}

	smoothTerrainNoise(x: number, y: number): number {
		const ix = Math.floor(x);
		const iy = Math.floor(y);
		const fx = x - ix;
		const fy = y - iy;
		const sx = fx * fx * (3 - 2 * fx);
		const sy = fy * fy * (3 - 2 * fy);
		const n00 = this.terrainNoise(ix, iy);
		const n10 = this.terrainNoise(ix + 1, iy);
		const n01 = this.terrainNoise(ix, iy + 1);
		const n11 = this.terrainNoise(ix + 1, iy + 1);
		return n00 * (1 - sx) * (1 - sy)
			+ n10 * sx * (1 - sy)
			+ n01 * (1 - sx) * sy
			+ n11 * sx * sy;
	}

	private localAvgHeight(hm: Float32Array, cx: number, cy: number, r: number): number {
		let sum = 0;
		let count = 0;
		for (let dy = -r; dy <= r; dy++) {
			for (let dx = -r; dx <= r; dx++) {
				const px = cx + dx;
				const py = cy + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
					sum += hm[py * this.width + px];
					count++;
				}
			}
		}

		return count > 0 ? sum / count : 0.5;
	}

	// ── Structure generation from tiles ──────────────────────────

	/** Auto-generate 3D structures from 2D house/wall data. */
	buildStructuresFromTiles(): void {
		const urban = this.mode === 'city' || this.mode === 'village';

		// Pre-compute density per house (count of neighbours within radius)
		// Only needed for urban modes where house height varies by density.
		// Wilderness modes have 100k+ tree entries — O(n²) would freeze.
		const houseDensity = new Float32Array(this.houses.length);
		let maxDensity = 1;
		if (urban) {
			const densityRadius = 25;
			for (let i = 0; i < this.houses.length; i++) {
				const hi = this.houses[i];
				if (this.grid[hi.y * this.width + hi.x] === TILE_TREE_DECOR) {
					continue;
				}

				const cx = hi.x + hi.w / 2;
				const cy = hi.y + hi.h / 2;
				let count = 0;
				for (let j = 0; j < this.houses.length; j++) {
					if (i === j) {
						continue;
					}

					const hj = this.houses[j];
					if (this.grid[hj.y * this.width + hj.x] === TILE_TREE_DECOR) {
						continue;
					}

					const dx = (hj.x + hj.w / 2) - cx;
					const dy = (hj.y + hj.h / 2) - cy;
					if (dx * dx + dy * dy < densityRadius * densityRadius) {
						count++;
					}
				}

				houseDensity[i] = count;
			}

			for (const element of houseDensity) {
				if (element > maxDensity) {
					maxDensity = element;
				}
			}
		}

		// Dominant tree type derived from seed (matches macroworld zoning)
		const dominantTree = Math.abs(this.seed * 2_654_435_761) % TREE_TYPES;

		// Houses → boxes
		for (const [idx, h] of this.houses.entries()) {
			const isTree = this.grid[h.y * this.width + h.x] === TILE_TREE_DECOR;
			if (isTree) {
				// ~80% dominant type, ~20% random variation
				const tp = this.rng.random() < 0.8
					? dominantTree
					: this.rng.randInt(0, TREE_TYPES - 1);
				this.structures.push(this.makeStructure({
					tag: 'tree',
					shape: 'rect',
					x: h.x + h.w / 2,
					y: h.y + h.h / 2,
					w: 0, l: 0,
					height: 3 + this.rng.randFloat(0, 4),
					rotation: h.rotation,
					roofTexture: 'tree_top',
					wallTexture: 'tree_trunk',
					solid: false,
					sprite: true,
					spriteColor: String(tp),
				}));
			} else {
				// Height based on mode: villages are 1 story, cities 1–3
				const density = houseDensity[idx] / maxDensity;
				const stories = this.mode === 'village'
					? 1
					: 1 + Math.floor(density * 2.5 + this.rng.randFloat(0, 0.5));
				const storyHeight = this.mode === 'village' ? 2.5 : 3;
				this.structures.push(this.makeStructure({
					tag: 'house',
					shape: 'rect',
					x: h.x + h.w / 2,
					y: h.y + h.h / 2,
					w: h.w, l: h.h,
					height: stories * storyHeight,
					rotation: h.rotation,
					roofTexture: this.mode === 'village' ? 'roof_thatch' : (urban ? 'roof_tile' : 'ruin_roof'),
					wallTexture: this.mode === 'village' ? 'house_wood' : (urban ? 'house_wall' : 'wall_ruin'),
					solid: true,
					sprite: false,
				}));
			}
		}

		// Walls → wall segments as thin boxes + towers as cylinders
		for (const wall of this.walls) {
			const isVillageWall = this.mode === 'village';
			const wallHeight = isVillageWall
				? 2.5 + this.rng.randFloat(0, 1)
				: 4 + this.rng.randFloat(0, 2);
			const wallTex = isVillageWall ? 'wall_wood' : 'wall_stone';
			const wallTopTex = isVillageWall ? 'palisade_top' : 'wall_top';
			const towerTopTex = isVillageWall ? 'palisade_top' : 'tower_top';
			const towerExtra = isVillageWall ? 0.5 : 2;
			const towerDiam = isVillageWall ? 2 : 3;
			const segments = getWallSegments(wall);
			for (const seg of segments) {
				if (seg.isGate) {
					continue;
				}

				const mx = (seg.p1.x + seg.p2.x) / 2;
				const my = (seg.p1.y + seg.p2.y) / 2;
				const dx = seg.p2.x - seg.p1.x;
				const dy = seg.p2.y - seg.p1.y;
				const segLength = Math.sqrt(dx * dx + dy * dy);
				const angle = Math.atan2(dy, dx);

				this.structures.push(this.makeStructure({
					tag: 'wall',
					shape: 'rect',
					x: mx, y: my,
					w: segLength, l: isVillageWall ? 1 : 1.5,
					height: wallHeight,
					rotation: angle,
					roofTexture: wallTopTex,
					wallTexture: wallTex,
					solid: true,
					sprite: false,
				}));
			}

			// Towers at non-gate vertices
			for (const p of wall.nodes) {
				const angle = Math.atan2(p.y - wall.centerY, p.x - wall.centerX);
				if (isGateAngle(wall, angle)) {
					continue;
				}

				this.structures.push(this.makeStructure({
					tag: 'tower',
					shape: 'circle',
					x: p.x, y: p.y,
					w: towerDiam, l: towerDiam,
					height: wallHeight + towerExtra,
					rotation: 0,
					roofTexture: towerTopTex,
					wallTexture: wallTex,
					solid: true,
					sprite: false,
				}));
			}

			// Gate-edge towers flanking each gate opening
			const gateTowers = getGateTowerPoints(segments);
			for (const pt of gateTowers) {
				this.structures.push(this.makeStructure({
					tag: 'tower',
					shape: 'circle',
					x: pt.x, y: pt.y,
					w: towerDiam, l: towerDiam,
					height: wallHeight + towerExtra,
					rotation: 0,
					roofTexture: towerTopTex,
					wallTexture: wallTex,
					solid: true,
					sprite: false,
				}));
			}
		}
	}

	protected makeStructure(partial: Omit<Structure, 'id' | 'state'> & {state?: StructureState}): Structure {
		return {
			id: this.nextStructureId++,
			state: 'active',
			...partial,
		};
	}

	// ── Grid primitives ──────────────────────────────────────────

	markLineOnGrid(x1: number, y1: number, x2: number, y2: number, value: number, width = 1): void {
		const _dx0 = x2 - x1;
		const _dy0 = y2 - y1;
		const dist = Math.sqrt(_dx0 * _dx0 + _dy0 * _dy0);
		const steps = Math.ceil(dist * 2);
		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			let x = x1 + (_dx0 * t);
			let y = y1 + (_dy0 * t);
			if (i > 0 && i < steps) {
				x += this.rng.randFloat(-0.8, 0.8);
				y += this.rng.randFloat(-0.8, 0.8);
			}

			const ix = Math.floor(x);
			const iy = Math.floor(y);
			const hw = Math.floor(width / 2);
			for (let dy = -hw; dy <= hw; dy++) {
				for (let dx = -hw; dx <= hw; dx++) {
					const px = ix + dx;
					const py = iy + dy;
					if (px >= 0 && px < this.width && py >= 0 && py < this.height
						&& this.grid[py * this.width + px] !== TILE_HOUSE) {
						this.grid[py * this.width + px] = value;
					}
				}
			}
		}
	}

	/** Mark an organic main road and store centerline. */
	markOrganicMainRoad(x1: number, y1: number, x2: number, y2: number, baseAngle: number): void {
		const _dx1 = x2 - x1;
		const _dy1 = y2 - y1;
		const dist = Math.sqrt(_dx1 * _dx1 + _dy1 * _dy1);
		const steps = Math.ceil(dist);
		const streetCells = new Set<number>();
		const centerline: Point[] = [];

		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			let x = x1 + ((x2 - x1) * t);
			let y = y1 + ((y2 - y1) * t);
			if (t > 0.08 && t < 0.92) {
				const waveFreq = 0.015;
				const waveAmp = 3.5 + this.rng.randFloat(-1, 1);
				const perpAngle = baseAngle + (Math.PI / 2);
				const offset = Math.sin((t * dist * waveFreq) + this.rng.worldSeed) * waveAmp;
				x += Math.cos(perpAngle) * offset;
				y += Math.sin(perpAngle) * offset;
			}

			centerline.push({x, y});
			const ix = Math.floor(x);
			const iy = Math.floor(y);
			for (let dy = -this.streetWidth; dy <= this.streetWidth; dy++) {
				for (let dx = -this.streetWidth; dx <= this.streetWidth; dx++) {
					const px = ix + dx;
					const py = iy + dy;
					if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
						streetCells.add((py * this.width) + px);
					}
				}
			}
		}

		this.mainRoadPaths.push(centerline);
		for (const idx of streetCells) {
			this.grid[idx] = TILE_ROAD;
		}
	}

	/** Mark a street segment, removing overlapping houses. Returns houses removed. */
	markStreetAndRemoveHouses(x1: number, y1: number, x2: number, y2: number): number {
		const streetCells = new Set<number>();
		const _dx2 = x2 - x1;
		const _dy2 = y2 - y1;
		const dist = Math.sqrt(_dx2 * _dx2 + _dy2 * _dy2);
		const steps = Math.ceil(dist * 2);
		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			const cx = Math.floor(x1 + (_dx2 * t));
			const cy = Math.floor(y1 + (_dy2 * t));
			for (let dy = -this.streetWidth; dy <= this.streetWidth; dy++) {
				for (let dx = -this.streetWidth; dx <= this.streetWidth; dx++) {
					const px = cx + dx;
					const py = cy + dy;
					if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
						streetCells.add((py * this.width) + px);
					}
				}
			}
		}

		const toRemove: number[] = [];
		for (let i = 0; i < this.houses.length; i++) {
			const h = this.houses[i];
			if (this.houseOverlaps(h, streetCells)) {
				toRemove.push(i);
				this.clearHouseFromGrid(h);
			}
		}

		for (let i = toRemove.length - 1; i >= 0; i--) {
			this.houses.splice(toRemove[i], 1);
		}

		for (const idx of streetCells) {
			this.grid[idx] = TILE_ROAD;
		}

		return toRemove.length;
	}

	private houseOverlaps(h: House, cells: Set<number>): boolean {
		for (let hy = h.y; hy < h.y + h.h; hy++) {
			for (let hx = h.x; hx < h.x + h.w; hx++) {
				if (cells.has((hy * this.width) + hx)) {
					return true;
				}
			}
		}

		return false;
	}

	private clearHouseFromGrid(h: House): void {
		for (let hy = h.y; hy < h.y + h.h; hy++) {
			for (let hx = h.x; hx < h.x + h.w; hx++) {
				if (hx >= 0 && hx < this.width && hy >= 0 && hy < this.height
					&& this.grid[(hy * this.width) + hx] === TILE_HOUSE) {
					this.grid[(hy * this.width) + hx] = TILE_EMPTY;
				}
			}
		}
	}

	terrainNoise(x: number, y: number): number {
		let value = (x * 374_761_393) ^ (y * 668_265_263) ^ (this.rng.worldSeed * 2_246_822_519);
		value = (value ^ (value >>> 13)) * 1_274_126_177;
		value ^= value >>> 16;
		return (value >>> 0) / 4_294_967_295;
	}

	// ── Shared helpers ─────────────────────────────────────────

	/** Check if any cell within radius matches the given tile type. */
	hasNearbyTile(cx: number, cy: number, tile: number, radius: number): boolean {
		for (let dy = -radius; dy <= radius; dy++) {
			for (let dx = -radius; dx <= radius; dx++) {
				const px = cx + dx;
				const py = cy + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height
					&& this.grid[(py * this.width) + px] === tile) {
					return true;
				}
			}
		}

		return false;
	}
}

// ── Free functions ──────────────────────────────────────────────

export function segmentIntersection(a1: Point, a2: Point, b1: Point, b2: Point): Point | undefined {
	const dx1 = a2.x - a1.x;
	const dy1 = a2.y - a1.y;
	const dx2 = b2.x - b1.x;
	const dy2 = b2.y - b1.y;
	const denom = (dx1 * dy2) - (dy1 * dx2);
	if (Math.abs(denom) < 0.001) {
		return undefined;
	}

	const t = (((b1.x - a1.x) * dy2) - ((b1.y - a1.y) * dx2)) / denom;
	const u = (((b1.x - a1.x) * dy1) - ((b1.y - a1.y) * dx1)) / denom;
	if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
		return {x: a1.x + (t * dx1), y: a1.y + (t * dy1)};
	}

	return undefined;
}

/**
 * Generate traversability grid from a MapData.
 * Splits wall segments at gate boundaries for correct passability.
 */
export function generateTraversability(data: MapData): Uint8Array {
	const {grid, width, height, walls} = data;
	const trav = new Uint8Array(width * height);

	for (let i = 0; i < trav.length; i++) {
		const tile = grid[i];
		trav[i] = (tile === TILE_HOUSE || tile === TILE_WALL) ? 0 : 255;
	}

	for (const wall of walls) {
		const segments = getWallSegments(wall);
		for (const seg of segments) {
			if (seg.isGate) {
				continue;
			}

			const _sdx = seg.p2.x - seg.p1.x;
			const _sdy = seg.p2.y - seg.p1.y;
			const dist = Math.sqrt(_sdx * _sdx + _sdy * _sdy);
			const steps = Math.ceil(dist * 2);
			for (let k = 0; k <= steps; k++) {
				const t = k / steps;
				const x = Math.floor(seg.p1.x + ((seg.p2.x - seg.p1.x) * t));
				const y = Math.floor(seg.p1.y + ((seg.p2.y - seg.p1.y) * t));
				blockCell(trav, width, height, x, y, 1);
			}

			// Tower at non-gate start vertex
			const startAngle = Math.atan2(seg.p1.y - wall.centerY, seg.p1.x - wall.centerX);
			if (!isGateAngle(wall, startAngle)) {
				blockTower(trav, width, height, seg.p1, 3);
			}
		}

		// Block gate-edge towers (flanking each gate opening)
		const gateTowers = getGateTowerPoints(segments);
		for (const pt of gateTowers) {
			blockTower(trav, width, height, pt, 3);
		}
	}

	return trav;
}

export function getTraversabilityData(data: MapData): {
	width: number;
	height: number;
	data: Uint8Array;
	heightData: Uint8Array;
	roadData: Uint8Array;
	iceData: Uint8Array;
} {
	const {grid, width, height} = data;
	const length = width * height;
	const trav = generateTraversability(data);
	const heightData = new Uint8Array(length).fill(128);
	const roadData = new Uint8Array(length);
	const iceData = new Uint8Array(length).fill(0);
	for (let i = 0; i < length; i++) {
		roadData[i] = grid[i] === TILE_ROAD ? 255 : 0;
	}

	return {
		width, height, data: trav, heightData, roadData, iceData,
	};
}

function blockCell(data: Uint8Array, width: number, height: number, x: number, y: number, radius = 0): void {
	for (let dy = -radius; dy <= radius; dy++) {
		for (let dx = -radius; dx <= radius; dx++) {
			const px = x + dx;
			const py = y + dy;
			if (px >= 0 && px < width && py >= 0 && py < height) {
				data[(py * width) + px] = 0;
			}
		}
	}
}

function blockTower(data: Uint8Array, width: number, height: number, center: Point, radius: number): void {
	for (let ty = -radius; ty <= radius; ty++) {
		for (let tx = -radius; tx <= radius; tx++) {
			if ((tx * tx) + (ty * ty) <= radius * radius) {
				blockCell(data, width, height, Math.floor(center.x + tx), Math.floor(center.y + ty), 0);
			}
		}
	}
}

// ── Wall sub-segmentation for gates ─────────────────────────────

export type WallSegment = {
	p1: Point;
	p2: Point;
	isGate: boolean;
};

/**
 * Split each polygon edge of a wall into sub-segments, clipping at gate
 * boundaries so that gate openings are geometrically precise regardless
 * of wall vertex density.
 */
export function getWallSegments(wall: WallRing): WallSegment[] {
	const result: WallSegment[] = [];
	const {nodes, centerX, centerY, gateAngles, gateHalfArc} = wall;

	for (let i = 0; i < nodes.length; i++) {
		const p1 = nodes[i];
		const p2 = nodes[(i + 1) % nodes.length];
		splitEdgeByGates(result, p1, p2, centerX, centerY, gateAngles, gateHalfArc);
	}

	return result;
}

function splitEdgeByGates(
	out: WallSegment[],
	p1: Point, p2: Point,
	cx: number, cy: number,
	gateAngles: number[], gateHalfArc: number,
): void {
	if (gateAngles.length === 0) {
		out.push({p1, p2, isGate: false});
		return;
	}

	// Collect t-values where the edge crosses gate boundary angles
	const cuts: Array<{t: number; entering: boolean}> = [];
	const steps = 32; // Sample the edge for angle transitions
	let previousInGate = isAngleInAnyGate(Math.atan2(p1.y - cy, p1.x - cx), gateAngles, gateHalfArc);

	for (let s = 1; s <= steps; s++) {
		const t = s / steps;
		const x = p1.x + ((p2.x - p1.x) * t);
		const y = p1.y + ((p2.y - p1.y) * t);
		const angle = Math.atan2(y - cy, x - cx);
		const inGate = isAngleInAnyGate(angle, gateAngles, gateHalfArc);
		if (inGate !== previousInGate) {
			// Binary search for precise crossing
			const preciseT = binarySearchBoundary(p1, p2, cx, cy, gateAngles, gateHalfArc, (s - 1) / steps, t, previousInGate);
			cuts.push({t: preciseT, entering: inGate});
		}

		previousInGate = inGate;
	}

	if (cuts.length === 0) {
		// Entire edge is either gate or wall
		out.push({p1, p2, isGate: previousInGate});
		return;
	}

	// Sort by t
	cuts.sort((a, b) => a.t - b.t);

	let currentIsGate = isAngleInAnyGate(Math.atan2(p1.y - cy, p1.x - cx), gateAngles, gateHalfArc);
	let lastPoint = p1;
	let lastT = 0;

	for (const cut of cuts) {
		if (cut.t - lastT > 0.001) {
			const midPoint = lerpPoint(p1, p2, cut.t);
			out.push({p1: lastPoint, p2: midPoint, isGate: currentIsGate});
			lastPoint = midPoint;
		}

		lastT = cut.t;
		currentIsGate = cut.entering;
	}

	// Final segment
	if (1 - lastT > 0.001) {
		out.push({p1: lastPoint, p2, isGate: currentIsGate});
	}
}

function binarySearchBoundary(
	p1: Point, p2: Point,
	cx: number, cy: number,
	gateAngles: number[], gateHalfArc: number,
	tLow: number, tHigh: number,
	lowIsGate: boolean,
): number {
	for (let iter = 0; iter < 12; iter++) {
		const tMid = (tLow + tHigh) / 2;
		const x = p1.x + ((p2.x - p1.x) * tMid);
		const y = p1.y + ((p2.y - p1.y) * tMid);
		const angle = Math.atan2(y - cy, x - cx);
		const midIsGate = isAngleInAnyGate(angle, gateAngles, gateHalfArc);
		if (midIsGate === lowIsGate) {
			tLow = tMid;
		} else {
			tHigh = tMid;
		}
	}

	return (tLow + tHigh) / 2;
}

function isAngleInAnyGate(angle: number, gateAngles: number[], gateHalfArc: number): boolean {
	for (const ga of gateAngles) {
		if (angularDistance(ga, angle) <= gateHalfArc) {
			return true;
		}
	}

	return false;
}

function lerpPoint(a: Point, b: Point, t: number): Point {
	return {x: a.x + ((b.x - a.x) * t), y: a.y + ((b.y - a.y) * t)};
}

/** Extract the two boundary points flanking each gate opening. */
export function getGateTowerPoints(segments: WallSegment[]): Point[] {
	const points: Point[] = [];
	for (let i = 0; i < segments.length; i++) {
		const seg = segments[i];
		const previous = segments[(i - 1 + segments.length) % segments.length];
		const next = segments[(i + 1) % segments.length];
		if (seg.isGate) {
			if (!previous.isGate) {
				points.push(seg.p1);
			}

			if (!next.isGate) {
				points.push(seg.p2);
			}
		}
	}

	return points;
}

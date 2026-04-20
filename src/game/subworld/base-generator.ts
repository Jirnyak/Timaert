// === Base map generator — shared grid + heightmap foundation ===

/** Integer hash noise — deterministic, position-based. */
import {
	TILE_EMPTY, TILE_ROAD, TILE_HOUSE, TILE_WALL,
	TILE_GRASS, TILE_FIELD, TILE_TREE_DECOR,
	TILE_WATER, TILE_SHORE, TILE_SQUARE, TILE_ROCK,
	type Point, type StreetNode, type StreetEdge,
	type House, type FieldPlot, type WallRing, type MapData,
	type SubworldMode, type Structure, type StructureState,
	type NeighborGrid, type EdgeAnchor,
	CellFeature, Dir, neighborIdx,
	MapRng, angularDistance, isGateAngle,
	computeEdgeAnchors, anchorForDir,
	Biome, biomeGroundTile,
} from './map-data';

export function terrainNoise(x: number, y: number, worldSeed: number): number {
	let value = (x * 374_761_393) ^ (y * 668_265_263) ^ (worldSeed * 2_246_822_519);
	value = (value ^ (value >>> 13)) * 1_274_126_177;
	value ^= value >>> 16;
	return (value >>> 0) / 4_294_967_295;
}

/**
 * Subworld water plane height (0..1 in heightmap space).
 * Terrain below this is submerged. Rendered at WATER_LEVEL × HEIGHT_SCALE world units.
 */
export const WATER_LEVEL = 0.4;

// ── Biome configuration table ───────────────────────────────────
// Per-biome parameters that drive all generators universally.
// Extend BiomeConfig when adding new biome-sensitive behaviors.

export type BiomeConfig = {
	/** Base tree density (0..1). 0 = no trees, 0.35 = dense forest. */
	treeDensity: number;
	/** Tree scatter step (lower = denser grid). */
	treeStep: number;
	/** Tree size range [min, max] (billboard w). */
	treeSize: readonly [number, number];
	/** Height noise amplitude multiplier (1 = normal, >1 = mountainous). */
	heightScale: number;
	/** Whether this biome generates swamp pools (terrain dips). */
	swampPools: boolean;
	/** Whether heightmap should use dune-like rolling (desert). */
	duneNoise: boolean;
};

const BIOME_CONFIGS: readonly BiomeConfig[] = [
	/* Tundra */ {
		treeDensity: 0.018, treeStep: 6, treeSize: [2, 3],
		heightScale: 0.8, swampPools: false, duneNoise: false,
	},
	/* Taiga */ {
		treeDensity: 0.2, treeStep: 3, treeSize: [2, 4],
		heightScale: 1, swampPools: false, duneNoise: false,
	},
	/* Snow */ {
		treeDensity: 0.025, treeStep: 5, treeSize: [2, 3],
		heightScale: 0.9, swampPools: false, duneNoise: false,
	},
	/* Valley */ {
		treeDensity: 0.05, treeStep: 4, treeSize: [2, 3],
		heightScale: 1, swampPools: false, duneNoise: false,
	},
	/* Meadow */ {
		treeDensity: 0.035, treeStep: 4, treeSize: [2, 3],
		heightScale: 1, swampPools: false, duneNoise: false,
	},
	/* Swamp */ {
		treeDensity: 0.08, treeStep: 3, treeSize: [2, 4],
		heightScale: 0.3, swampPools: true, duneNoise: false,
	},
	/* Desert */ {
		treeDensity: 0.004, treeStep: 8, treeSize: [2, 3],
		heightScale: 0.6, swampPools: false, duneNoise: true,
	},
	/* Steppe */ {
		treeDensity: 0.018, treeStep: 5, treeSize: [2, 3],
		heightScale: 0.8, swampPools: false, duneNoise: false,
	},
	/* Tropics */ {
		treeDensity: 0.25, treeStep: 2, treeSize: [2, 5],
		heightScale: 1, swampPools: false, duneNoise: false,
	},
	/* Water (biome 9) — ocean/lake cells: nearly all submerged */ {
		treeDensity: 0, treeStep: 16, treeSize: [2, 3],
		heightScale: 0.5, swampPools: false, duneNoise: false,
	},
];

/** Get the biome config for a Biome enum value. Falls back to Meadow. */
export function getBiomeConfig(biome: Biome): BiomeConfig {
	return BIOME_CONFIGS[biome] ?? BIOME_CONFIGS[Biome.Meadow];
}

/** Abstract base — subclassed by CityGenerator and NatureGenerator. */
export abstract class BaseMapGenerator {
	readonly width: number;
	readonly height: number;
	readonly mode: SubworldMode;
	readonly streetWidth: number;
	readonly rng: MapRng;
	grid: Uint8Array;

	/** Biome of the center cell. Set via setNeighbors() or defaults to Meadow. */
	biome: Biome = Biome.Meadow;

	/** Macroworld sea level (0..1). Universal water plane height. */
	seaLevel = 0.4;

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

	/** Deterministic edge anchors for cross-cell stitching. */
	edgeAnchors: EdgeAnchor[] = [];

	/** Global tile origin of this cell (cellX * width, cellY * height). */
	private globalOffsetX = 0;
	private globalOffsetY = 0;
	/** Shared world-level seed for seamless noise across all cells. */
	private worldNoiseSeed = 0;

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

	/** Store neighbor grid and pre-compute edge anchors + global offsets. */
	setNeighbors(neighbors: NeighborGrid): void {
		this.neighborGrid = neighbors;
		this.seaLevel = neighbors.seaLevel;
		this.edgeAnchors = computeEdgeAnchors(neighbors, this.width);
		// Global tile coordinates: every cell in the world uses the same
		// coordinate space so noise is continuous across boundaries.
		const c = neighbors.center;
		this.globalOffsetX = c.cellX * this.width;
		this.globalOffsetY = c.cellY * this.height;
		this.biome = c.biome;
		// Recover the macroworld seed (SubworldScreen encodes it as
		// gameState.seed + cellX * 1000 + cellY). Fall back to per-cell
		// seed if the inverse doesn't look right.
		this.worldNoiseSeed = c.seed - c.cellX * 1000 - c.cellY;
	}

	/** Get anchor for a specific compass direction. */
	anchorFor(dir: Dir): EdgeAnchor | undefined {
		return anchorForDir(this.edgeAnchors, dir);
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
			biome: this.biome,
			houses: this.houses,
			walls: this.walls,
			fieldPlots: this.fieldPlots,
			mainRoadPaths: this.mainRoadPaths,
			streetNodes: this.streetNodes,
			streetEdges: this.streetEdges,
			heightmap,
			structures: this.structures,
			waterLevel: WATER_LEVEL,
		};
	}

	// ── Heightmap generation ─────────────────────────────────────

	/**
	 * Generate terrain heightmap using multi-octave coherent noise.
	 * Noise is evaluated in **global** tile coordinates so adjacent cells
	 * produce perfectly continuous terrain at their shared edges.
	 *
	 * Heightmap is ABSOLUTE: 0 = deep ocean floor, WATER_LEVEL = water surface,
	 * 1 = highest mountain peak. macroHeight from the macroworld directly
	 * determines base elevation. Bilinear interpolation across 9 cells
	 * creates smooth transitions at all boundaries.
	 */
	generateHeightmap(): Float32Array {
		const {width, height, globalOffsetX: gox, globalOffsetY: goy} = this;
		const hm = new Float32Array(width * height);
		const cfg = getBiomeConfig(this.biome);

		// ── Per-cell mountain influence ──
		// Mountain cells: 1.0 base + 0.5 per adjacent mountain → solo peaks to mountain walls.
		// Non-mountain cells: 0.1 base + 0.15 per adjacent mountain → gentle rolling, foothills near mountains.
		// Bilinearly interpolated so mountain → plain is a smooth gradient.
		const mountainScales = new Float32Array(9);
		if (this.neighborGrid) {
			const {cells} = this.neighborGrid;
			for (let i = 0; i < 9; i++) {
				const isMtn = cells[i].feature === CellFeature.Mountain;
				// Count that cell's own mountain neighbors (within the 3×3)
				const cy = Math.floor(i / 3);
				const cx = i % 3;
				let adjMtn = 0;
				if (cx > 0 && cells[i - 1].feature === CellFeature.Mountain) {
					adjMtn++;
				}

				if (cx < 2 && cells[i + 1].feature === CellFeature.Mountain) {
					adjMtn++;
				}

				if (cy > 0 && cells[i - 3].feature === CellFeature.Mountain) {
					adjMtn++;
				}

				if (cy < 2 && cells[i + 3].feature === CellFeature.Mountain) {
					adjMtn++;
				}

				mountainScales[i] = isMtn
					? 0.3
					: 0.1 + adjMtn * 0.15;
			}
		} else {
			mountainScales.fill(0.1);
		}

		// ── Per-cell mountain ridge weight ──
		// Binary: 1.0 for mountain cells, 0.0 otherwise.
		// Bilinearly interpolated in the inner loop to create smooth
		// transition from ridged terrain to regular terrain.
		const ridgeWeights = new Float32Array(9);
		let needsRidgeNoise = false;
		if (this.neighborGrid) {
			const {cells} = this.neighborGrid;
			for (let i = 0; i < 9; i++) {
				if (cells[i].feature === CellFeature.Mountain) {
					ridgeWeights[i] = 1;
					needsRidgeNoise = true;
				}
			}
		}

		// ── Per-cell macro gradient ──
		// Max height difference to 4-connected neighbors in the 3×3 grid.
		// Higher gradient → rougher local terrain at cell transitions.
		const macroGradients = new Float32Array(9);
		if (this.neighborGrid) {
			const {cells} = this.neighborGrid;
			for (let cy = 0; cy < 3; cy++) {
				for (let cx = 0; cx < 3; cx++) {
					const idx = cy * 3 + cx;
					const {macroHeight: mh} = cells[idx];
					let maxDiff = 0;
					if (cx > 0) {
						maxDiff = Math.max(maxDiff, Math.abs(mh - cells[idx - 1].macroHeight));
					}

					if (cx < 2) {
						maxDiff = Math.max(maxDiff, Math.abs(mh - cells[idx + 1].macroHeight));
					}

					if (cy > 0) {
						maxDiff = Math.max(maxDiff, Math.abs(mh - cells[idx - 3].macroHeight));
					}

					if (cy < 2) {
						maxDiff = Math.max(maxDiff, Math.abs(mh - cells[idx + 3].macroHeight));
					}

					macroGradients[idx] = maxDiff;
				}
			}
		}

		// ── Grid traits: single scan of 3×3 biome / feature / landmark ──
		// Extract every per-cell BiomeConfig property in one pass.
		// Boolean flags skip entire inner-loop branches when no cell in
		// the neighborhood has the corresponding effect.
		const heightScales = new Float32Array(9);
		const duneFactors = new Float32Array(9);
		const swampFactors = new Float32Array(9);
		let needsDuneNoise = false;
		let needsSwampPools = false;
		if (this.neighborGrid) {
			const {cells} = this.neighborGrid;
			for (let i = 0; i < 9; i++) {
				const bc = getBiomeConfig(cells[i].biome);
				heightScales[i] = bc.heightScale;
				if (bc.duneNoise) {
					duneFactors[i] = 1;
					needsDuneNoise = true;
				}

				if (bc.swampPools) {
					swampFactors[i] = 1;
					needsSwampPools = true;
				}
			}
		} else {
			heightScales.fill(cfg.heightScale);
			if (cfg.duneNoise) {
				duneFactors.fill(1);
				needsDuneNoise = true;
			}

			if (cfg.swampPools) {
				swampFactors.fill(1);
				needsSwampPools = true;
			}
		}

		// Road/square smoothing: skip when generator produced no roads
		const needsRoadSmoothing = this.streetNodes.length > 0
			|| this.mainRoadPaths.length > 0;

		// Per-column and per-row bilinear weights (computed once, reused per pixel)
		const bxCol = new Uint8Array(width);
		const sxCol = new Float32Array(width);
		for (let x = 0; x < width; x++) {
			const fx = x / width + 1;
			bxCol[x] = Math.min(2, Math.max(0, Math.floor(fx - 0.5)));
			sxCol[x] = Math.max(0, Math.min(1, (fx - 0.5) - bxCol[x]));
		}

		const byRow = new Uint8Array(height);
		const syRow = new Float32Array(height);
		for (let y = 0; y < height; y++) {
			const fy = y / height + 1;
			byRow[y] = Math.min(2, Math.max(0, Math.floor(fy - 0.5)));
			syRow[y] = Math.max(0, Math.min(1, (fy - 0.5) - byRow[y]));
		}

		// ── Macro-blended base via height remapping ──
		// Remap each cell's macroHeight:
		//   Water cells: depth proportional to how far below seaLevel.
		//     At seaLevel → WATER_LEVEL (shoreline), at 0 → 0 (deep ocean).
		//     Squared curve pushes most water cells well below WATER_LEVEL.
		//   Land  cells: [seaLevel, 1] → [WATER_LEVEL, 1] (linear).
		let macroBase: Float32Array | undefined;
		if (this.neighborGrid) {
			const {cells} = this.neighborGrid;
			const {seaLevel} = this;
			const landScale = (1 - WATER_LEVEL) / (1 - seaLevel);
			const remapped = new Float32Array(9);
			for (let i = 0; i < 9; i++) {
				const mh = cells[i].macroHeight;
				if (cells[i].biome === Biome.Water) {
					// T=1 at shoreline (mh=seaLevel), t=0 at deep ocean (mh=0)
					const t = Math.max(0, mh / seaLevel);
					// Squared: shallow water drops off fast, deep ocean near 0
					remapped[i] = t * t * WATER_LEVEL;
				} else {
					remapped[i] = WATER_LEVEL + (mh - seaLevel) * landScale;
				}
			}

			// Bilinear blend remapped heights for the centre cell's pixel grid.
			macroBase = new Float32Array(width * height);
			for (let y = 0; y < height; y++) {
				const iby = byRow[y];
				const isy = syRow[y];
				const iby2 = Math.min(2, iby + 1);
				for (let x = 0; x < width; x++) {
					const ibx = bxCol[x];
					const isx = sxCol[x];
					const ibx2 = Math.min(2, ibx + 1);
					macroBase[y * width + x] = remapped[iby * 3 + ibx] * (1 - isx) * (1 - isy)
						+ remapped[iby * 3 + ibx2] * isx * (1 - isy)
						+ remapped[iby2 * 3 + ibx] * (1 - isx) * isy
						+ remapped[iby2 * 3 + ibx2] * isx * isy;
				}
			}
		}

		// Mountain mask for rock texture gating (reused in tile sync).
		// Stores bilinearly-interpolated ridgeWeight per pixel.
		const mountainMask = needsRidgeNoise
			? new Float32Array(width * height)
			: undefined;

		// Multi-octave noise in global coordinates for seamless terrain
		for (let y = 0; y < height; y++) {
			const by = byRow[y];
			const sy = syRow[y];
			const by2 = Math.min(2, by + 1);
			for (let x = 0; x < width; x++) {
				const gx = x + gox;
				const gy = y + goy;
				let noise = 0;
				noise += this.smoothTerrainNoise(gx * 0.008, gy * 0.008) * 0.5;
				noise += this.smoothTerrainNoise(gx * 0.02, gy * 0.02) * 0.25;
				noise += this.smoothTerrainNoise(gx * 0.06, gy * 0.06) * 0.125;
				// Normalize to 0–1 range (noise returns 0–1 per octave)
				noise = Math.max(0, Math.min(1, noise / 0.875));

				// Bilinear-blended parameters — smooth cross-biome transition
				const bx = bxCol[x];
				const sx = sxCol[x];
				const bx2 = Math.min(2, bx + 1);
				const w00 = (1 - sx) * (1 - sy);
				const w10 = sx * (1 - sy);
				const w01 = (1 - sx) * sy;
				const w11 = sx * sy;

				// Mountain weight for rock texture gating
				if (needsRidgeNoise && mountainMask) {
					mountainMask[y * width + x] = ridgeWeights[by * 3 + bx] * w00
						+ ridgeWeights[by * 3 + bx2] * w10
						+ ridgeWeights[by2 * 3 + bx] * w01
						+ ridgeWeights[by2 * 3 + bx2] * w11;
				}

				const localHS = heightScales[by * 3 + bx] * w00
					+ heightScales[by * 3 + bx2] * w10
					+ heightScales[by2 * 3 + bx] * w01
					+ heightScales[by2 * 3 + bx2] * w11;
				const localGrad = macroGradients[by * 3 + bx] * w00
					+ macroGradients[by * 3 + bx2] * w10
					+ macroGradients[by2 * 3 + bx] * w01
					+ macroGradients[by2 * 3 + bx2] * w11;
				const localMtn = mountainScales[by * 3 + bx] * w00
					+ mountainScales[by * 3 + bx2] * w10
					+ mountainScales[by2 * 3 + bx] * w01
					+ mountainScales[by2 * 3 + bx2] * w11;

				// Smooth manifold: foothills near mountains rise gradually;
				// plains stay flat. Mountain ridges replace uniform noise.
				let macroH = macroBase ? macroBase[y * width + x] : 0.5;

				// Swamp flattening: pull terrain toward just above water level.
				// Applied per-pixel AFTER bilinear blend so neighbor heights
				// don't create elevated rims / crater edges around swamps.
				if (needsSwampPools) {
					const sf = swampFactors[by * 3 + bx] * w00
						+ swampFactors[by * 3 + bx2] * w10
						+ swampFactors[by2 * 3 + bx] * w01
						+ swampFactors[by2 * 3 + bx2] * w11;
					if (sf > 0.01) {
						const swampTarget = WATER_LEVEL + 0.06;
						macroH += (swampTarget - macroH) * sf * 0.85;
					}
				}

				let h;
				if (macroBase) {
					const relief = macroH * macroH + localGrad;
					h = macroH + (noise - 0.5) * relief * localHS * localMtn;
				} else {
					h = 0.5 + (noise - 0.5) * localHS * 0.5;
				}

				// Mountain ridged terrain — natural chains with grand valleys
				if (needsRidgeNoise) {
					const rw = ridgeWeights[by * 3 + bx] * w00
						+ ridgeWeights[by * 3 + bx2] * w10
						+ ridgeWeights[by2 * 3 + bx] * w01
						+ ridgeWeights[by2 * 3 + bx2] * w11;
					h = this.applyMountainRidges(h, gx, gy, macroH, rw);
				}

				// Dune rolling: fades smoothly at desert biome edges
				if (needsDuneNoise) {
					const duneFactor = duneFactors[by * 3 + bx] * w00
						+ duneFactors[by * 3 + bx2] * w10
						+ duneFactors[by2 * 3 + bx] * w01
						+ duneFactors[by2 * 3 + bx2] * w11;
					if (duneFactor > 0.01) {
						h += (this.smoothTerrainNoise(gx * 0.012, gy * 0.018) - 0.5) * 0.15 * duneFactor;
					}
				}

				// Swamp lowlands: smooth gentle undulations near water level.
				// Two continuous noise layers create marshy terrain — no hard
				// thresholds, no crater edges. The terrain gently rolls above
				// and below water producing natural boggy depressions.
				if (needsSwampPools) {
					const swampFactor = swampFactors[by * 3 + bx] * w00
						+ swampFactors[by * 3 + bx2] * w10
						+ swampFactors[by2 * 3 + bx] * w01
						+ swampFactors[by2 * 3 + bx2] * w11;
					if (swampFactor > 0.01) {
						// Broad undulation — gently pushes areas below water
						const lowland = this.smoothTerrainNoise(gx * 0.006 + 200, gy * 0.006 + 200);
						// Medium-scale boggy depressions
						const bog = this.smoothTerrainNoise(gx * 0.025, gy * 0.025);
						// Both continuous — no threshold, smooth everywhere
						const dip = (1 - lowland) * 0.05 + (1 - bog) * 0.04;
						h -= dip * swampFactor;
					}
				}

				hm[y * width + x] = Math.max(0, Math.min(1, h));
			}
		}

		// Flatten roads and squares (skip when no roads in this cell)
		if (needsRoadSmoothing) {
			for (let y = 0; y < height; y++) {
				for (let x = 0; x < width; x++) {
					const tile = this.grid[y * width + x];
					if (tile === TILE_ROAD || tile === TILE_SQUARE) {
						const i = y * width + x;
						hm[i] = this.localAvgHeight(hm, x, y, 3);
					}
				}
			}

			// Smooth pass for road/square transitions (×2)
			this.smoothRoadHeights(hm);
			this.smoothRoadHeights(hm);
		}

		// ── Heightmap → tile sync ──────────────────────────────────
		// The heightmap is the source of truth for what's underwater.
		// Sync tiles both ways so 2D and 3D views always agree.
		// For Water biome cells, above-water terrain uses surrounding
		// land biome tiles so the natural slope looks like land, not mud.
		const landTile = this.biome === Biome.Water
			? this.dominantLandTile()
			: biomeGroundTile(this.biome);
		const shoreTop = WATER_LEVEL + 0.05;
		// Rock coverage: only in mountain biome cells (and their blended range).
		// 50% at h=0.6 (300 world units), 100% at h=0.8 (400 units).
		// Uses smoothTerrainNoise for coherent patches, not noise dots.
		const rockFloor = 0.5; // Start of possible rock
		const rockFull = 0.8; // 100% rock above this
		for (let y = 0; y < height; y++) {
			for (let x = 0; x < width; x++) {
				const i = y * width + x;
				const h = hm[i];
				if (h < WATER_LEVEL) {
					if (this.grid[i] !== TILE_WATER) {
						this.grid[i] = TILE_WATER;
					}
				} else if (h < shoreTop && this.grid[i] !== TILE_ROAD
					&& this.grid[i] !== TILE_HOUSE && this.grid[i] !== TILE_WALL
					&& this.grid[i] !== TILE_SQUARE) {
					this.grid[i] = TILE_SHORE;
				} else if (this.grid[i] === TILE_WATER || this.grid[i] === TILE_SHORE) {
					this.grid[i] = landTile;
				}

				// Rock patches — only in mountain cells and their blended range
				const mtnW = mountainMask ? mountainMask[i] : 0;
				if (mtnW > 0.01 && h >= rockFloor && this.grid[i] !== TILE_ROAD
					&& this.grid[i] !== TILE_HOUSE && this.grid[i] !== TILE_WALL
					&& this.grid[i] !== TILE_SQUARE && this.grid[i] !== TILE_WATER) {
					const t = Math.min(1, (h - rockFloor) / (rockFull - rockFloor));
					const rockProb = Math.min(1, t * t * 4.5); // ~50% at h=0.6 (300u), 100% at h=0.8 (400u)
					const gx = x + gox;
					const gy = y + goy;
					const n = this.smoothTerrainNoise(gx * 0.03 + 500, gy * 0.03 + 500);
					if (n < rockProb * mtnW) {
						this.grid[i] = TILE_ROCK;
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

	/**
	 * Ridged multifractal noise blended over base height for mountain areas.
	 * Domain warping creates organic ridge curvature. Sharp peaks and
	 * deep valleys replace the uniform chaotic plateau.
	 */
	private applyMountainRidges(h: number, gx: number, gy: number, macroH: number, rw: number): number {
		if (rw <= 0.01) {
			return h;
		}

		// Domain warp for organic ridge shapes (global coords → seamless)
		const wx = gx + (this.smoothTerrainNoise(gx * 0.002 + 71.7, gy * 0.002) - 0.5) * 90;
		const wy = gy + (this.smoothTerrainNoise(gx * 0.002, gy * 0.002 + 31.1) - 0.5) * 90;

		// 4-octave ridged multifractal with amplitude feedback.
		// Invert abs(noise) → sharp peaks at zero-crossings, square for
		// sharpness, weight next octave by current signal → detail on ridges.
		let ridge = 0;
		let amp = 1;
		let wt = 1;

		let sig = this.smoothTerrainNoise(wx * 0.004, wy * 0.004);
		sig = 1 - Math.abs(2 * sig - 1);
		sig *= sig;
		sig = Math.min(sig * wt, 1);
		wt = Math.min(1, sig * 2.5);
		ridge += sig * amp;
		amp *= 0.45;

		sig = this.smoothTerrainNoise(wx * 0.009, wy * 0.009);
		sig = 1 - Math.abs(2 * sig - 1);
		sig *= sig;
		sig = Math.min(sig * wt, 1);
		wt = Math.min(1, sig * 2.5);
		ridge += sig * amp;
		amp *= 0.45;

		sig = this.smoothTerrainNoise(wx * 0.02, wy * 0.02);
		sig = 1 - Math.abs(2 * sig - 1);
		sig *= sig;
		sig = Math.min(sig * wt, 1);
		wt = Math.min(1, sig * 2.5);
		ridge += sig * amp;
		amp *= 0.45;

		sig = this.smoothTerrainNoise(wx * 0.045, wy * 0.045);
		sig = 1 - Math.abs(2 * sig - 1);
		sig *= sig;
		ridge += Math.min(sig * wt, 1) * amp;

		ridge = Math.min(1, ridge * 0.55);

		// Valley floor → peak: dramatic range for grand panoramic views
		const valleyFloor = Math.max(WATER_LEVEL + 0.08, macroH * 0.5);
		const peak = Math.min(1, macroH + 0.08);
		const mtnH = valleyFloor + ridge * (peak - valleyFloor);

		// Blend: rw=1 deep inside mountains, fades at edges
		const blend = Math.min(1, rw * 1.5);
		return h * (1 - blend) + mtnH * blend;
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

	/** One smooth pass over road/square tiles in the heightmap. */
	private smoothRoadHeights(hm: Float32Array): void {
		const {width, height} = this;
		for (let y = 1; y < height - 1; y++) {
			for (let x = 1; x < width - 1; x++) {
				const tile = this.grid[y * width + x];
				if (tile === TILE_ROAD || tile === TILE_SQUARE) {
					const i = y * width + x;
					hm[i] = (hm[i] * 2 + hm[i - 1] + hm[i + 1]
						+ hm[i - width] + hm[i + width]) / 6;
				}
			}
		}
	}

	// ── Structure generation from tiles ──────────────────────────

	/**
	 * Map cell temperature + per-tree variation to species index.
	 * Mirrors the macroworld GLSL bands in tree-spawner.ts exactly so
	 * forests look consistent when zooming in.
	 * 0:Oak 1:Cherry 2:Birch 3:Autumn 4:Pine 5:Willow 6:Jungle
	 */
	temperatureToTreeType(variation: number): number {
		const t = this.neighborGrid?.center.temperature ?? 0.5;
		if (t < 0.2) {
			return 4; // Coldest → Pine only
		}

		if (t < 0.35) {
			return variation < 0.45 ? 4 : 2; // Cold → Pine / Birch
		}

		if (t < 0.5) {
			return variation < 0.45 ? 2 : 3; // Cool → Birch / Autumn
		}

		if (t < 0.65) {
			return variation < 0.4 ? 0 : (variation < 0.7 ? 3 : 5); // Temperate → Oak / Autumn / Willow
		}

		if (t < 0.8) {
			return variation < 0.35 ? 1 : (variation < 0.65 ? 0 : 5); // Warm → Cherry / Oak / Willow
		}

		return 6; // Tropical → Jungle
	}

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

		// Tree species from cell temperature + per-tree variation — matches macroworld bands
		// Use position-based hash for deterministic tree species across cells.
		const gox = this.globalOffsetX;
		const goy = this.globalOffsetY;

		// Houses → boxes
		for (const [idx, h] of this.houses.entries()) {
			const isTree = this.grid[h.y * this.width + h.x] === TILE_TREE_DECOR;
			if (isTree) {
				// Position-based hashes for seamless tree appearance
				const gx = h.x + gox;
				const gy = h.y + goy;
				const typeHash = this.terrainNoise(gx * 11 + 33_333, gy * 17 + 44_444);
				const heightHash = this.terrainNoise(gx * 19 + 55_555, gy * 23 + 66_666);
				const tp = this.temperatureToTreeType(typeHash);
				this.structures.push(this.makeStructure({
					tag: 'tree',
					shape: 'rect',
					x: h.x + h.w / 2,
					y: h.y + h.h / 2,
					w: 0, l: 0,
					height: 3 + heightHash * 4,
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
				const rawHeight = stories * storyHeight;
				const houseHeight = Math.max(5, rawHeight);
				this.structures.push(this.makeStructure({
					tag: 'house',
					shape: 'rect',
					x: h.x + h.w / 2,
					y: h.y + h.h / 2,
					w: h.w, l: h.h,
					height: houseHeight,
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
				? 4 + this.rng.randFloat(0, 2)
				: 10 + this.rng.randFloat(0, 5);
			const wallTex = isVillageWall ? 'wall_wood' : 'wall_stone';
			const wallTopTex = isVillageWall ? 'palisade_top' : 'wall_top';
			const towerTopTex = isVillageWall ? 'palisade_top' : 'tower_top';
			const towerHeight = isVillageWall ? wallHeight + 2 : 20;
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
					height: towerHeight,
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
					height: towerHeight,
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

	/**
	 * Integer hash noise — deterministic, position-based.
	 * Uses worldNoiseSeed (same for all cells) so noise is continuous
	 * across cell boundaries when called with global coordinates.
	 */
	terrainNoise(x: number, y: number): number {
		return terrainNoise(x, y, this.worldNoiseSeed);
	}

	/**
	 * Per-cell noise — uses the cell's own seed, so each cell gets
	 * unique local patterns (field shapes, house density, etc.).
	 * NOT seamless across cell boundaries — use terrainNoise for that.
	 */
	localNoise(x: number, y: number): number {
		let value = (x * 374_761_393) ^ (y * 668_265_263) ^ (this.rng.worldSeed * 2_246_822_519);
		value = (value ^ (value >>> 13)) * 1_274_126_177;
		value ^= value >>> 16;
		return (value >>> 0) / 4_294_967_295;
	}

	/** Smooth interpolated version of localNoise (per-cell, not seamless). */
	smoothLocalNoise(x: number, y: number): number {
		const ix = Math.floor(x);
		const iy = Math.floor(y);
		const fx = x - ix;
		const fy = y - iy;
		const sx = fx * fx * (3 - 2 * fx);
		const sy = fy * fy * (3 - 2 * fy);
		const n00 = this.localNoise(ix, iy);
		const n10 = this.localNoise(ix + 1, iy);
		const n01 = this.localNoise(ix, iy + 1);
		const n11 = this.localNoise(ix + 1, iy + 1);
		return n00 * (1 - sx) * (1 - sy)
			+ n10 * sx * (1 - sy)
			+ n01 * (1 - sx) * sy
			+ n11 * sx * sy;
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

	// ── Universal terrain base ──────────────────────────────────

	/**
	 * Find the most common land biome among neighbors.
	 * Used for Water cells so above-water terrain matches surrounding land.
	 */
	private dominantLandTile(): number {
		if (!this.neighborGrid) {
			return biomeGroundTile(Biome.Meadow);
		}

		const counts = new Uint8Array(10); // One per Biome enum
		for (const c of this.neighborGrid.cells) {
			if (c.biome !== Biome.Water) {
				counts[c.biome]++;
			}
		}

		let best = Biome.Meadow;
		let bestCount = 0;
		for (const [i, count] of counts.entries()) {
			if (count > bestCount) {
				bestCount = count;
				best = i as Biome;
			}
		}

		return biomeGroundTile(best);
	}

	/**
	 * Fill grid with biome ground tile and blend near edges where
	 * neighbor biomes differ. Uses global smooth noise so both cells
	 * sharing an edge produce the exact same stochastic pattern →
	 * seamless biome transition with organic patch shapes.
	 */
	layGrassBase(): void {
		const tile = biomeGroundTile(this.biome);
		this.grid.fill(tile);
		if (!this.neighborGrid || this.biome === Biome.Water) {
			return;
		}

		// Collect neighbor directions with different land biomes
		const blends: Array<{dir: Dir; tile: number}> = [];
		for (let d = 0; d < 8; d++) {
			const nb = this.neighborGrid.cells[neighborIdx(d as Dir)].biome;
			if (nb !== this.biome && nb !== Biome.Water) {
				blends.push({dir: d as Dir, tile: biomeGroundTile(nb)});
			}
		}

		if (blends.length === 0) {
			return;
		}

		const BLEND = 64;
		const {width: w, height: h} = this;
		for (let y = 0; y < h; y++) {
			if (y < BLEND || y >= h - BLEND) {
				// Top/bottom rows: scan full width
				for (let x = 0; x < w; x++) {
					this.blendBiomeTile(x, y, blends, BLEND);
				}
			} else {
				// Middle rows: only left + right strips
				for (let x = 0; x < BLEND; x++) {
					this.blendBiomeTile(x, y, blends, BLEND);
				}

				for (let x = w - BLEND; x < w; x++) {
					this.blendBiomeTile(x, y, blends, BLEND);
				}
			}
		}
	}

	/** Stochastic tile replacement toward nearest different-biome edge. */
	private blendBiomeTile(
		x: number, y: number,
		blends: ReadonlyArray<{dir: Dir; tile: number}>,
		blendDepth: number,
	): void {
		let bestDist = blendDepth;
		let bestTile = -1;
		for (const {dir, tile} of blends) {
			const dist = this.edgeDistance(dir, x, y, blendDepth);
			if (dist < bestDist) {
				bestDist = dist;
				bestTile = tile;
			}
		}

		if (bestTile < 0) {
			return;
		}

		const t = 1 - bestDist / blendDepth;
		const n = this.smoothTerrainNoise(
			(x + this.globalOffsetX + 0.5) * 0.12,
			(y + this.globalOffsetY + 0.5) * 0.12,
		);
		if (n < t * t * 0.65) {
			this.grid[y * this.width + x] = bestTile;
		}
	}

	// ── Universal tree scattering ───────────────────────────────

	/**
	 * Scatter trees across the cell with smooth density that depends on:
	 * - Biome config (desert=almost none, tropics=dense)
	 * - Feature type (forest cells get boosted density)
	 * - Neighbor features (forest neighbors boost density at shared edges)
	 * - Distance from center (urban modes suppress trees near center)
	 *
	 * @param clearRadius — radius around center where no trees grow (for urban).
	 *   If a WallRing is provided, trees are suppressed inside the wall instead.
	 */
	scatterUniversalTrees(clearRadius = 0, wall?: WallRing): void {
		const cfg = getBiomeConfig(this.biome);
		const isForest = this.mode === 'forest'
			|| this.neighborGrid?.center.feature === CellFeature.Tree;
		const baseDensity = isForest ? Math.max(cfg.treeDensity, 0.3) : cfg.treeDensity;
		const step = isForest ? Math.max(2, cfg.treeStep - 1) : cfg.treeStep;
		const treeW = this.rng.randInt(cfg.treeSize[0], cfg.treeSize[1]);

		// Mountain altitude thins forest globally for this cell.
		let mtnScale = 1;
		if (this.neighborGrid) {
			let mtnCount = 0;
			for (let i = 0; i < 9; i++) {
				if (this.neighborGrid.cells[i].feature === CellFeature.Mountain) {
					mtnCount++;
				}
			}

			if (mtnCount > 0) {
				mtnScale = 1 - (mtnCount / 9) * 0.85;
			}
		}

		// Pre-compute spatially-varying density field that blends toward neighbors
		const densityField = this.computeDensityField(baseDensity, mtnScale);

		// ── Globally-aligned grid scan ──
		// Use global tile coordinates and position-based hash so tree
		// placement is identical regardless of which cell computes it.
		// Adjacent cells produce perfectly continuous tree distributions.
		const gox = this.globalOffsetX;
		const goy = this.globalOffsetY;
		// Align start to global multiples of step
		const gStartX = gox + ((step - (gox % step)) % step);
		const gStartY = goy + ((step - (goy % step)) % step);
		const gEndX = gox + this.width;
		const gEndY = goy + this.height;

		for (let gy = gStartY; gy < gEndY; gy += step) {
			for (let gx = gStartX; gx < gEndX; gx += step) {
				const x = gx - gox;
				const y = gy - goy;
				const idx = y * this.width + x;
				const tile = this.grid[idx];
				// Only place on empty or biome ground tiles
				if (tile !== TILE_EMPTY && !isGroundTile(tile)) {
					continue;
				}

				// Skip near roads and fields
				if (this.hasNearbyTile(x, y, TILE_ROAD, 2)
					|| this.hasNearbyTile(x, y, TILE_FIELD, 1)) {
					continue;
				}

				// Urban clear zone — suppress inside wall or within clearRadius
				if (wall && this.isInsideWall(wall, x + 0.5, y + 0.5)) {
					continue;
				}

				if (clearRadius > 0) {
					const cdx = x - this.centerX;
					const cdy = y - this.centerY;
					if (cdx * cdx + cdy * cdy < clearRadius * clearRadius) {
						continue;
					}
				}

				let density = densityField[idx];

				// Urban gradient: ramp density from 0 at center to full at edges
				if (clearRadius > 0) {
					const cdx = x - this.centerX;
					const cdy = y - this.centerY;
					const dist = Math.sqrt(cdx * cdx + cdy * cdy);
					const edgeR = Math.min(this.width, this.height) * 0.48;
					const t = Math.min(1, Math.max(0, (dist - clearRadius) / (edgeR - clearRadius)));
					density *= t * t;
				}

				// FBM noise gate for clustering (global coords for seamless patterns)
				const n1 = this.smoothTerrainNoise(gx * 0.015, gy * 0.015);
				const n2 = this.smoothTerrainNoise(gx * 0.04, gy * 0.04);
				const fbm = n1 * 0.7 + n2 * 0.3;
				const ns = Math.max(0, Math.min(1, (fbm - 0.2) / 0.5));
				density *= ns;

				// Position-based hash for placement — deterministic from
				// global coords so adjacent cells produce identical results.
				const posHash = this.terrainNoise(gx * 7 + 12_345, gy * 13 + 67_890);
				if (posHash < density) {
					const w = treeW;
					const h = treeW;
					// Position-based rotation for seamless consistency
					const rotHash = this.terrainNoise(gx * 3 + 11_111, gy * 5 + 22_222);
					this.houses.push({
						x, y, w, h,
						rotation: (rotHash - 0.5) * 0.6,
					});
					this.grid[idx] = TILE_TREE_DECOR;
				}
			}
		}
	}

	/**
	 * Compute a per-tile density field that smoothly blends between
	 * the center cell's density and each neighbor's density near edges.
	 * At the cell boundary, density converges to the average of both
	 * cells — guaranteeing seamless forest↔grassland transitions.
	 */
	private computeDensityField(centerDensity: number, mtnScale: number): Float32Array {
		const field = new Float32Array(this.width * this.height);
		const scaled = centerDensity * mtnScale;
		field.fill(scaled);

		if (!this.neighborGrid) {
			return field;
		}

		const fadeDepth = Math.min(this.width, this.height) * 0.45;

		// Pre-compute each neighbor's effective density
		const neighborDensities = new Float32Array(9);
		for (let i = 0; i < 9; i++) {
			const nc = this.neighborGrid.cells[i];
			const ncfg = getBiomeConfig(nc.biome);
			const nIsForest = nc.feature === CellFeature.Tree;
			neighborDensities[i] = (nIsForest
				? Math.max(ncfg.treeDensity, 0.3)
				: ncfg.treeDensity) * mtnScale;
		}

		// Blend toward each neighbor's density near that edge
		for (let d = 0; d < 8; d++) {
			const ni = neighborIdx(d as Dir);
			const nDensity = neighborDensities[ni];
			// Skip if densities are similar — no visible seam to fix
			if (Math.abs(nDensity - scaled) < 0.01) {
				continue;
			}

			for (let y = 0; y < this.height; y++) {
				for (let x = 0; x < this.width; x++) {
					const edgeDist = this.edgeDistance(d as Dir, x, y, fadeDepth);
					if (edgeDist >= fadeDepth) {
						continue;
					}

					const t = 1 - edgeDist / fadeDepth;
					const s = t * t * (3 - 2 * t); // Smoothstep
					const idx = y * this.width + x;
					// Blend from center density toward neighbor density
					field[idx] += (nDensity - scaled) * s * 0.5;
				}
			}
		}

		return field;
	}

	/** Distance from the edge facing direction `d`. */
	private edgeDistance(d: Dir, x: number, y: number, _fallback: number): number {
		switch (d) {
			case Dir.N: {return y;}
			case Dir.S: {return this.height - 1 - y;}
			case Dir.W: {return x;}
			case Dir.E: {return this.width - 1 - x;}
			case Dir.NW: {return Math.min(x, y);}
			case Dir.NE: {return Math.min(this.width - 1 - x, y);}
			case Dir.SW: {return Math.min(x, this.height - 1 - y);}
			case Dir.SE: {return Math.min(this.width - 1 - x, this.height - 1 - y);}
		}
	}

	/** Point-in-wall test for urban clear zones. */
	protected isInsideWall(wall: WallRing, px: number, py: number): boolean {
		// Quick radius checks for early-out
		const dx = px - wall.centerX;
		const dy = py - wall.centerY;
		const distance = Math.sqrt(dx * dx + dy * dy);
		if (distance <= wall.avgRadius * 0.72) {
			return true;
		}

		if (distance >= wall.avgRadius * 1.35) {
			return false;
		}

		const {nodes} = wall;
		let inside = false;
		for (let i = 0, j = nodes.length - 1; i < nodes.length; j = i++) {
			const xi = nodes[i].x;
			const yi = nodes[i].y;
			const xj = nodes[j].x;
			const yj = nodes[j].y;
			if ((yi > py) !== (yj > py)
				&& px < ((xj - xi) * (py - yi)) / ((yj - yi) || 0.000_01) + xi) {
				inside = !inside;
			}
		}

		return inside;
	}
}

// ── Free functions ──────────────────────────────────────────────

/**
 * Returns true for any tile that represents biome ground (including all 9 biomes).
 * Used to decide where trees/features can be placed.
 */
export function isGroundTile(tile: number): boolean {
	return tile === TILE_GRASS
		|| (tile >= 8 && tile <= 15); // TILE_TUNDRA(8) .. TILE_TROPICS(15)
}

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
		// Only physical structures block movement; water/shore are walkable (costs SP)
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

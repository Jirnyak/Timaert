// === Forest generator — dense woodland subworld ===
//
// Produces a tile map with winding paths through dense tree cover.
// Trees are placed via the universal scatterUniversalTrees() in base generator.
// Glades are derived from global-coordinate noise so they tile seamlessly
// across adjacent forest cells.

import {
	TILE_TREE_DECOR,
	type MapData,
	type NeighborGrid,
	biomeGroundTile,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

/** Glade scan step — every Nth tile is checked as a potential glade center. */
const GLADE_STEP = 48;
/** Noise threshold above which a glade is carved (0..1, higher = fewer glades). */
const GLADE_THRESHOLD = 0.72;
/** Base glade radius in tiles. */
const GLADE_RADIUS_MIN = 6;
const GLADE_RADIUS_MAX = 16;

export class ForestGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'forest', 1);
	}

	generateTiles(_density: number, neighbors?: NeighborGrid): void {
		if (neighbors) {
			this.setNeighbors(neighbors);
		}

		this.layGrassBase();
		this.carvePaths();
		this.scatterUniversalTrees();
		this.scatterGlades();
	}

	/** Carve winding main paths from center to edges + toward stitched neighbors. */
	private carvePaths(): void {
		if (this.edgeAnchors.length === 0) {
			return;
		}

		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		for (const anchor of this.edgeAnchors) {
			const angle = Math.atan2(anchor.y - this.centerY, anchor.x - this.centerX);
			this.markOrganicMainRoad(this.centerX, this.centerY, anchor.x, anchor.y, angle);
		}
	}

	/**
	 * Scatter small grass clearings using global-coordinate noise.
	 * Glade positions are derived from smoothTerrainNoise (world-seed-based)
	 * so they tile seamlessly across adjacent forest cells.
	 */
	private scatterGlades(): void {
		const groundTile = this.grid[this.centerY * this.width + this.centerX]
			|| biomeGroundTile(this.biome);
		const gox = this.neighborGrid?.center.cellX ?? 0;
		const goy = this.neighborGrid?.center.cellY ?? 0;
		const gOffX = gox * this.width;
		const gOffY = goy * this.height;

		// Scan on a globally-aligned coarse grid; noise decides glade presence
		const startGX = gOffX + ((GLADE_STEP - (gOffX % GLADE_STEP)) % GLADE_STEP);
		const startGY = gOffY + ((GLADE_STEP - (gOffY % GLADE_STEP)) % GLADE_STEP);
		const endGX = gOffX + this.width;
		const endGY = gOffY + this.height;

		for (let gy = startGY; gy < endGY; gy += GLADE_STEP) {
			for (let gx = startGX; gx < endGX; gx += GLADE_STEP) {
				// Slow-varying noise determines glade presence
				const n = this.smoothTerrainNoise(gx * 0.007 + 777, gy * 0.007 + 888);
				if (n < GLADE_THRESHOLD) {
					continue;
				}

				// Radius from second noise channel
				const rn = this.terrainNoise(gx * 5 + 99_999, gy * 7 + 88_888);
				const radius = GLADE_RADIUS_MIN
					+ Math.floor(rn * (GLADE_RADIUS_MAX - GLADE_RADIUS_MIN + 1));

				// Local tile position
				const lx = gx - gOffX;
				const ly = gy - gOffY;
				this.carveGlade(lx, ly, radius, groundTile);
			}
		}
	}

	/** Carve a single circular glade at (lx, ly), removing trees. */
	private carveGlade(lx: number, ly: number, radius: number, groundTile: number): void {
		for (let dy = -radius; dy <= radius; dy++) {
			for (let dx = -radius; dx <= radius; dx++) {
				if (dx * dx + dy * dy > radius * radius) {
					continue;
				}

				const px = lx + dx;
				const py = ly + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
					const idx = py * this.width + px;
					if (this.grid[idx] === 0 || this.grid[idx] === TILE_TREE_DECOR) {
						this.grid[idx] = groundTile;
					}
				}
			}
		}

		// Remove tree houses that fall inside the glade
		for (let i = this.houses.length - 1; i >= 0; i--) {
			const h = this.houses[i];
			if (this.grid[h.y * this.width + h.x] !== TILE_TREE_DECOR) {
				const ddx = h.x - lx;
				const ddy = h.y - ly;
				if (ddx * ddx + ddy * ddy <= radius * radius) {
					this.houses.splice(i, 1);
				}
			}
		}
	}
}

/** Functional entry point — used by the generator registry. */
export function generateForest(
	seed: number, width: number, height: number,
	_parameter?: number, neighbors?: NeighborGrid,
): MapData {
	const gen = new ForestGenerator(seed, width, height);
	gen.generateTiles(0, neighbors);
	return gen.toMapData();
}

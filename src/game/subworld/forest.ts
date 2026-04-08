// === Forest generator — dense woodland subworld ===
//
// Produces a tile map with winding paths through dense tree cover.
// Trees are placed via the universal scatterUniversalTrees() in base generator.

import {
	TILE_TREE_DECOR,
	type MapData,
	type NeighborGrid,
	biomeGroundTile,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

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

	/** Scatter small grass clearings in the forest. */
	private scatterGlades(): void {
		const gladeCount = this.rng.randInt(4, 10);
		for (let i = 0; i < gladeCount; i++) {
			const gx = this.rng.randInt(50, this.width - 50);
			const gy = this.rng.randInt(50, this.height - 50);
			const radius = this.rng.randInt(6, 16);
			this.carveGlade(gx, gy, radius);
		}
	}

	/** Carve a single circular glade at (gx, gy). */
	private carveGlade(gx: number, gy: number, radius: number): void {
		const groundTile = this.grid[this.centerY * this.width + this.centerX] || biomeGroundTile(this.biome);
		for (let dy = -radius; dy <= radius; dy++) {
			for (let dx = -radius; dx <= radius; dx++) {
				if (dx * dx + dy * dy > radius * radius) {
					continue;
				}

				const px = gx + dx;
				const py = gy + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
					const idx = py * this.width + px;
					if (this.grid[idx] === 0 || this.grid[idx] === TILE_TREE_DECOR) {
						this.grid[idx] = groundTile;
					}
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

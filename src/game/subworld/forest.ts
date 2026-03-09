// === Forest generator — dense woodland subworld ===
//
// Produces a tile map with winding paths through dense tree cover.
// Trees are placed as houses (rendered as foliage circles by the renderer).

import {
	TILE_ROAD, TILE_TREE_DECOR, TILE_GRASS, type MapData,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

export class ForestGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'forest', 1);
	}

	generateTiles(_density: number): void {
		this.carvePaths();
		this.fillTrees();
		this.scatterGlades();
	}

	/** Carve 4 winding main paths from center to edges. */
	private carvePaths(): void {
		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		const pathCount = this.rng.randInt(3, 5);
		for (let i = 0; i < pathCount; i++) {
			const angle = (i * (Math.PI * 2) / pathCount)
				+ this.rng.randFloat(-0.4, 0.4);
			const tx = this.centerX + (Math.cos(angle) * this.width * 0.6);
			const ty = this.centerY + (Math.sin(angle) * this.height * 0.6);
			this.markOrganicMainRoad(this.centerX, this.centerY, tx, ty, angle);
		}

		// Side trails
		const trailCount = this.rng.randInt(6, 14);
		for (let i = 0; i < trailCount; i++) {
			this.growStreetBranch();
		}
	}

	/** Fill empty tiles with trees (represented as houses for rendering). */
	private fillTrees(): void {
		for (let y = 2; y < this.height - 2; y += 2) {
			for (let x = 2; x < this.width - 2; x += 2) {
				const idx = y * this.width + x;
				if (this.grid[idx] !== 0) {
					continue;
				}

				// Skip near roads (small clearing around paths)
				if (this.hasNearbyTile(x, y, TILE_ROAD, 1)) {
					continue;
				}

				// 70% tree coverage
				if (this.rng.random() < 0.7) {
					const w = this.rng.randInt(2, 4);
					const h = this.rng.randInt(2, 4);
					this.houses.push({
						x, y, w, h, rotation: this.rng.randFloat(-0.3, 0.3),
					});
					this.grid[idx] = TILE_TREE_DECOR;
				}
			}
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
						this.grid[idx] = TILE_GRASS;
					}
				}
			}
		}
	}

	private hasNearbyTile(x: number, y: number, tile: number, radius: number): boolean {
		for (let dy = -radius; dy <= radius; dy++) {
			for (let dx = -radius; dx <= radius; dx++) {
				const nx = x + dx;
				const ny = y + dy;
				if (nx >= 0 && nx < this.width && ny >= 0 && ny < this.height
					&& this.grid[ny * this.width + nx] === tile) {
					return true;
				}
			}
		}

		return false;
	}
}

/** Functional entry point — used by the generator registry. */
export function generateForest(seed: number, width: number, height: number): MapData {
	const gen = new ForestGenerator(seed, width, height);
	gen.generateTiles(0);
	return gen.toMapData();
}

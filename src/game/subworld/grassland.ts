// === Grassland generator — open plains subworld ===
//
// Produces a tile map with gentle rolling grassland, sparse trees,
// and dirt trails connecting random points.

import {TILE_GRASS, TILE_TREE_DECOR, type MapData} from './map-data';
import {BaseMapGenerator} from './base-generator';

export class GrasslandGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'grassland', 1);
	}

	generateTiles(_density: number): void {
		this.layGrassBase();
		this.carveTrails();
		this.scatterTrees();
	}

	/** Fill the entire map with grass. */
	private layGrassBase(): void {
		for (let i = 0; i < this.grid.length; i++) {
			this.grid[i] = TILE_GRASS;
		}
	}

	/** Carve a few dirt trails across the open plains. */
	private carveTrails(): void {
		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		const trailCount = this.rng.randInt(2, 4);
		for (let i = 0; i < trailCount; i++) {
			const angle = (i * (Math.PI * 2) / trailCount)
				+ this.rng.randFloat(-0.5, 0.5);
			const tx = this.centerX + (Math.cos(angle) * this.width * 0.55);
			const ty = this.centerY + (Math.sin(angle) * this.height * 0.55);
			this.markOrganicMainRoad(this.centerX, this.centerY, tx, ty, angle);
		}

		// A few side branches
		const branchCount = this.rng.randInt(3, 8);
		for (let i = 0; i < branchCount; i++) {
			this.growStreetBranch();
		}
	}

	/** Scatter isolated trees and small copses across the grassland. */
	private scatterTrees(): void {
		const treeChance = 0.04; // ~4% coverage — sparse
		for (let y = 3; y < this.height - 3; y += 3) {
			for (let x = 3; x < this.width - 3; x += 3) {
				const idx = y * this.width + x;
				if (this.grid[idx] !== TILE_GRASS) {
					continue;
				}

				if (this.rng.random() < treeChance) {
					const w = this.rng.randInt(2, 3);
					const h = this.rng.randInt(2, 3);
					this.houses.push({
						x, y, w, h, rotation: this.rng.randFloat(-0.2, 0.2),
					});
					this.grid[idx] = TILE_TREE_DECOR;
				}
			}
		}
	}
}

/** Functional entry point — used by the generator registry. */
export function generateGrassland(seed: number, width: number, height: number): MapData {
	const gen = new GrasslandGenerator(seed, width, height);
	gen.generateTiles(0);
	return gen.toMapData();
}

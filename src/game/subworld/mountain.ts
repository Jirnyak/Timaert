// === Mountain generator — mountainous terrain subworld ===
//
// Produces a tile map with rugged highland terrain. The heightmap
// relief comes entirely from the universal formula (macroH² + gradient)
// in base-generator — no feature-specific amplification needed.
// This generator adds:
// - Biome-aware ground via layGrassBase()
// - Mountain passes (roads) connecting through edge anchors
// - Sparse high-altitude vegetation from scatterUniversalTrees()

import {
	TILE_ROAD, TILE_GRASS, TILE_WALL,
	type MapData, type NeighborGrid,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

export class MountainGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'mountain', 1);
	}

	generateTiles(_density: number, neighbors?: NeighborGrid): void {
		if (neighbors) {
			this.setNeighbors(neighbors);
		}

		this.layGrassBase();
		this.carvePasses();
		this.scatterUniversalTrees();
	}

	/**
	 * Carve mountain passes through edge anchors.
	 * Passes also clear cliff tiles along their route.
	 */
	private carvePasses(): void {
		if (this.edgeAnchors.length === 0) {
			return;
		}

		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		for (const anchor of this.edgeAnchors) {
			const angle = Math.atan2(anchor.y - this.centerY, anchor.x - this.centerX);
			this.markOrganicMainRoad(this.centerX, this.centerY, anchor.x, anchor.y, angle);
		}

		// Clear cliff tiles near roads (widen the pass)
		const clearRadius = 3;
		for (let y = 0; y < this.height; y++) {
			for (let x = 0; x < this.width; x++) {
				if (this.grid[y * this.width + x] !== TILE_WALL) {
					continue;
				}

				if (this.hasNearbyTile(x, y, TILE_ROAD, clearRadius)) {
					this.grid[y * this.width + x] = TILE_GRASS;
				}
			}
		}
	}
}

/** Functional entry point — used by the generator registry. */
export function generateMountain(
	seed: number, width: number, height: number,
	_parameter?: number, neighbors?: NeighborGrid,
): MapData {
	const gen = new MountainGenerator(seed, width, height);
	gen.generateTiles(0, neighbors);
	return gen.toMapData();
}

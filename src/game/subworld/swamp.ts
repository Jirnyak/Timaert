// === Swamp generator — boggy wetland subworld ===
//
// Produces a tile map with swamp terrain: patches of standing water
// (pools that sit below WATER_LEVEL), soggy ground, and scattered
// gnarled trees. The heightmap has terrain dips that flood naturally.
//
// Swamp pools are noise-driven depressions where terrain drops below
// WATER_LEVEL. The universal heightmap + tile-marking in base-generator
// handles the water naturally — this generator just ensures the ground
// tile is swamp biome and scatters appropriate vegetation.

import {
	type MapData, type NeighborGrid,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

export class SwampGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'swamp', 1);
	}

	generateTiles(_density: number, neighbors?: NeighborGrid): void {
		if (neighbors) {
			this.setNeighbors(neighbors);
		}

		this.layGrassBase();
		this.carveTrails();
		this.scatterUniversalTrees();
	}

	/** Carve muddy paths toward edge anchors. */
	private carveTrails(): void {
		if (this.edgeAnchors.length === 0) {
			return;
		}

		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		for (const anchor of this.edgeAnchors) {
			const angle = Math.atan2(anchor.y - this.centerY, anchor.x - this.centerX);
			this.markOrganicMainRoad(this.centerX, this.centerY, anchor.x, anchor.y, angle);
		}
	}
}

/** Functional entry point — used by the generator registry. */
export function generateSwamp(
	seed: number, width: number, height: number,
	_parameter?: number, neighbors?: NeighborGrid,
): MapData {
	const gen = new SwampGenerator(seed, width, height);
	gen.generateTiles(0, neighbors);
	return gen.toMapData();
}

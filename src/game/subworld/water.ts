// === Water generator — water biome cells ===
//
// Minimal generator for Water biome cells.
// Lays biome ground tiles and scatters trees. The coastline
// emerges naturally from the heightmap: macro heights of Water
// cells remap below WATER_LEVEL while land neighbors blend
// above it. The universal tile sync in generateHeightmap()
// converts anything below WATER_LEVEL to TILE_WATER — no
// explicit water placement needed.

import {
	type MapData, type NeighborGrid,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

export class WaterGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'water', 1);
	}

	generateTiles(_density: number, neighbors?: NeighborGrid): void {
		if (neighbors) {
			this.setNeighbors(neighbors);
		}

		this.layGrassBase();
		this.scatterUniversalTrees();
	}
}

/** Functional entry point — used by the generator registry. */
export function generateWater(
	seed: number, width: number, height: number,
	_parameter?: number, neighbors?: NeighborGrid,
): MapData {
	const gen = new WaterGenerator(seed, width, height);
	gen.generateTiles(0, neighbors);
	return gen.toMapData();
}

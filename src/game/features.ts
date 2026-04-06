// === Feature Layer — static world decorations between biome and landmark ===
//
// Layer 1 (Macroworld). Features are persistent visual elements on map cells.
// They sit between the terrain biome (GPU-computed) and landmarks/entities
// (cities, NPCs). Features do not alter the underlying biome.
//
// Three feature types:
//   Road     — 1-cell-width paths traced by road-network between landmarks
//   Tree     — CPU-spawned by tree-spawner, rendered as instanced sprites
//   Mountain — height-derived, visual overlay via mountain-spawner
//
// Cell structure (bottom → top):
//   1. Biome    — terrain type from height/moisture/temperature
//   2. Feature  — road, tree, or mountain (this module)
//   3. Landmark — settlement, dungeon, etc. (full entity object)

import type {TraversabilityData} from '../webgl/map-generator';

export enum FeatureType {
	None = 0,
	Road = 1,
	Tree = 2,
	Mountain = 3,
	DirtRoad = 4,
}

export type FeatureLayer = {
	width: number;
	height: number;
	data: Uint8Array; // FeatureType per cell
};

/**
 * Build the feature layer from terrain data, spawned positions, and road masks.
 * Priority (last writer wins): Mountain → Tree → DirtRoad → Road.
 *
 * @param roadMask - Output of generateRoadNetwork() (255 = road cell)
 * @param dirtRoadMask - Optional dirt-road traces for villages (255 = dirt road cell)
 */
export function buildFeatureLayer(
	tData: TraversabilityData,
	trees: ReadonlyArray<{x: number; y: number}>,
	mountainThreshold: number,
	roadMask: Uint8Array,
	dirtRoadMask?: Uint8Array,
): FeatureLayer {
	const {width, height} = tData;
	const data = new Uint8Array(width * height);

	// Pass 1: Mountains — cells above height threshold
	for (let i = 0; i < width * height; i++) {
		if (tData.heightData[i] / 255 >= mountainThreshold) {
			data[i] = FeatureType.Mountain;
		}
	}

	// Pass 2: Trees (overwrite mountains where placed)
	for (const t of trees) {
		const idx = t.y * width + t.x;
		if (idx >= 0 && idx < data.length) {
			data[idx] = FeatureType.Tree;
		}
	}

	// Pass 3: Dirt roads — village connector paths
	if (dirtRoadMask) {
		for (let i = 0; i < width * height; i++) {
			if (dirtRoadMask[i] > 0) {
				data[i] = FeatureType.DirtRoad;
			}
		}
	}

	// Pass 4: Roads — highest priority (traced 1-cell network)
	for (let i = 0; i < width * height; i++) {
		if (roadMask[i] > 0) {
			data[i] = FeatureType.Road;
		}
	}

	return {width, height, data};
}

/** Query the feature type at a given cell (torus-safe). */
export function getFeatureAt(layer: FeatureLayer, x: number, y: number): FeatureType {
	const wx = ((x % layer.width) + layer.width) % layer.width;
	const wy = ((y % layer.height) + layer.height) % layer.height;
	return layer.data[wy * layer.width + wx] as FeatureType;
}

// === Map factory — generator registry + unified entry point ===
//
// To add a new subworld type:
//   1. Create `<type>.ts` exporting a generate function
//   2. Import it here and call registerGenerator('<type>', fn)
//   3. Add the type to SubworldMode union in map-data.ts
// That's it — the factory, renderer, and traversability all work automatically.

import type {SubworldMode, SubworldMapData, MapData} from './map-data';
import {generateTraversability, getTraversabilityData} from './base-generator';
import {renderMap} from './map-renderer';
import {CityMapGenerator} from './city-generator';
import {generateForest} from './forest';
import {generateGrassland} from './grassland';
import {generateVillage} from './village';
import {generateRuin} from './ruin';

// ── Generator registry ──────────────────────────────────────────

/**
 * A generator function takes a seed, dimensions, and an optional numeric
 * parameter (population for cities/villages, difficulty for ruins, etc.)
 * and returns a raw MapData.
 */
export type GeneratorFn = (
	seed: number, width: number, height: number, parameter?: number,
) => MapData;

const registry = new Map<SubworldMode, GeneratorFn>();

/** Register a generator for a subworld mode. Called at module init time. */
export function registerGenerator(mode: SubworldMode, fn: GeneratorFn): void {
	registry.set(mode, fn);
}

registerGenerator('city', (seed, w, h, population = 1000) => {
	const gen = new CityMapGenerator(seed, w, h);
	gen.generateTiles(population);
	return gen.toMapData();
});

registerGenerator('forest', generateForest);
registerGenerator('grassland', generateGrassland);
registerGenerator('village', generateVillage);
registerGenerator('ruin', generateRuin);

// ── Public API ──────────────────────────────────────────────────

/**
 * Generate a complete subworld map.
 * Dispatches to the registered generator for the given mode,
 * renders the visual, and computes traversability — deterministically from seed.
 */
export function generateSubworldMap(
	seed: number, width: number, height: number,
	mode: SubworldMode, parameter = 0,
): SubworldMapData & {mapData: MapData} {
	const fn = registry.get(mode);
	if (!fn) {
		throw new Error(`No generator registered for subworld mode "${mode}"`);
	}

	const mapData = fn(seed, width, height, parameter);
	const visual = renderMap(mapData);
	const traversability = generateTraversability(mapData);

	return {
		visual,
		grid: traversability,
		tileGrid: mapData.grid,
		width: mapData.width,
		height: mapData.height,
		spawnX: mapData.spawnX,
		spawnY: mapData.spawnY,
		mapData,
	};
}

/** Get full traversability payload for GameScreen's pathfinding. */
export function getSubworldTraversabilityData(mapData: MapData): {
	width: number;
	height: number;
	data: Uint8Array;
	heightData: Uint8Array;
	roadData: Uint8Array;
	iceData: Uint8Array;
} {
	return getTraversabilityData(mapData);
}


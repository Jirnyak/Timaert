// === Map factory — generator registry + unified entry point ===
//
// To add a new subworld type:
//   1. Create `<type>.ts` exporting a generate function
//   2. Import it here and call registerGenerator('<type>', fn)
//   3. Add the type to SubworldMode union in map-data.ts
// That's it — the factory, renderer, and traversability all work automatically.

import type {
	SubworldMode, SubworldMapData, MapData, SavedSubworldData,
	Structure, NeighborGrid,
} from './map-data';
import {generateTraversability, getTraversabilityData} from './base-generator';
import {renderMap} from './map-renderer';
import {CityMapGenerator} from './city-generator';
import {generateForest} from './forest';
import {generateGrassland} from './grassland';
import {VillageGenerator} from './village';
import {generateRuin} from './ruin';
import {generateRoad} from './road-generator';
import {generateMountain} from './mountain';
import {generateWater} from './water';
import {generateSwamp} from './swamp';
import {generateSpire} from './spire';

// ── Generator registry ──────────────────────────────────────────

/**
 * A generator function takes a seed, dimensions, an optional numeric
 * parameter (population for cities/villages, difficulty for ruins, etc.)
 * and an optional NeighborGrid for seamless connectivity.
 * Returns a raw MapData.
 */
export type GeneratorFn = (
	seed: number, width: number, height: number,
	parameter?: number, neighbors?: NeighborGrid,
) => MapData;

const registry = new Map<SubworldMode, GeneratorFn>();

/** Register a generator for a subworld mode. Called at module init time. */
export function registerGenerator(mode: SubworldMode, fn: GeneratorFn): void {
	registry.set(mode, fn);
}

registerGenerator('city', (seed, w, h, population, neighbors) => {
	const gen = new CityMapGenerator(seed, w, h);
	// Math.max floor prevents 0-population cities (from combat deaths) generating no houses
	gen.generateTiles(Math.max(population ?? 1000, 50), neighbors);
	return gen.toMapData();
});

registerGenerator('forest', generateForest);
registerGenerator('grassland', generateGrassland);
registerGenerator('village', (seed, w, h, population, neighbors) => {
	const gen = new VillageGenerator(seed, w, h);
	gen.generateTiles(Math.max(population ?? 50, 10), neighbors);
	return gen.toMapData();
});
registerGenerator('ruin', generateRuin);
registerGenerator('road', generateRoad);
registerGenerator('mountain', generateMountain);
registerGenerator('water', generateWater);
registerGenerator('swamp', generateSwamp);
registerGenerator('spire', generateSpire);

// ── Saved subworld cache ────────────────────────────────────────

/**
 * In-memory cache of saved subworld data, keyed by macroworld cell.
 * Persists across subworld visits within a session.
 * GameScreen should save/load this via the save system.
 */
const savedSubworlds = new Map<string, SavedSubworldData>();

/** Get a cache key for a macroworld cell. */
function cellKey(seed: number, mode: SubworldMode): string {
	return `${seed}:${mode}`;
}

/** Save subworld data when the player leaves. */
export function saveSubworldData(seed: number, mode: SubworldMode, data: SavedSubworldData): void {
	savedSubworlds.set(cellKey(seed, mode), data);
}

/** Load previously saved subworld data, if any. */
export function loadSubworldData(seed: number, mode: SubworldMode): SavedSubworldData | undefined {
	return savedSubworlds.get(cellKey(seed, mode));
}

/** Get all saved subworld data for serialisation. */
export function getAllSavedSubworlds(): Map<string, SavedSubworldData> {
	return savedSubworlds;
}

/** Restore saved subworld data from deserialised state. */
export function restoreSavedSubworlds(data: Map<string, SavedSubworldData>): void {
	savedSubworlds.clear();
	for (const [key, value] of data) {
		savedSubworlds.set(key, value);
	}
}

// ── Regeneration logic ──────────────────────────────────────────

/**
 * Regenerate a subworld from saved data + new game context.
 *
 * Algorithm:
 * 1. Start with saved heightmap and structures.
 * 2. Generate fresh MapData from current context (population, etc.).
 * 3. Diff saved structures against fresh structures by tag:
 *    - If saved has MORE of a tag than fresh → mark excess as abandoned/withered.
 *    - If saved has FEWER of a tag than fresh → add new ones from fresh.
 *    - Existing active structures keep their positions (persistence).
 * 4. Return merged result.
 */
function regenerateFromSaved(
	saved: SavedSubworldData,
	fresh: MapData,
): MapData {
	// Decompress saved heightmap
	const heightmap = new Float32Array(saved.width * saved.height);
	for (let i = 0; i < heightmap.length; i++) {
		heightmap[i] = saved.heightmap[i] / 65_535;
	}

	// Group structures by tag
	const savedByTag = groupByTag(saved.structures);
	const freshByTag = groupByTag(fresh.structures);

	const merged: Structure[] = [];
	let nextId = saved.nextStructureId;

	// Process all tags from both saved and fresh
	const allTags = new Set([...savedByTag.keys(), ...freshByTag.keys()]);
	for (const tag of allTags) {
		const savedList = savedByTag.get(tag) ?? [];
		const freshList = freshByTag.get(tag) ?? [];

		if (savedList.length <= freshList.length) {
			// Keep all saved structures, update textures from fresh generation
			for (const [i, s] of savedList.entries()) {
				merged.push(freshList[i]
					? {...s, wallTexture: freshList[i].wallTexture, roofTexture: freshList[i].roofTexture}
					: s);
			}

			// Add new structures from fresh (ones beyond saved count)
			for (let i = savedList.length; i < freshList.length; i++) {
				const newStruct = {...freshList[i], id: nextId++};
				merged.push(newStruct);
			}
		} else {
			// More saved than fresh → mark excess as abandoned/withered
			for (const [i, element] of savedList.entries()) {
				if (i < freshList.length) {
					merged.push({...element, wallTexture: freshList[i].wallTexture, roofTexture: freshList[i].roofTexture});
				} else {
					// Mark excess as abandoned/withered based on type
					const abandoned = {...element};
					abandoned.state = tag === 'tree' ? 'withered' : 'abandoned';
					if (abandoned.state === 'abandoned') {
						abandoned.wallTexture = 'wall_ruin';
						abandoned.roofTexture = 'ruin_roof';
					}

					merged.push(abandoned);
				}
			}
		}
	}

	return {
		...fresh,
		heightmap,
		structures: merged,
	};
}

function groupByTag(structures: Structure[]): Map<string, Structure[]> {
	const map = new Map<string, Structure[]>();
	for (const s of structures) {
		let list = map.get(s.tag);
		if (!list) {
			list = [];
			map.set(s.tag, list);
		}

		list.push(s);
	}

	return map;
}

// ── Public API ──────────────────────────────────────────────────

/**
 * Generate a complete subworld map.
 * Dispatches to the registered generator for the given mode,
 * renders the visual, and computes traversability — deterministically from seed.
 *
 * If saved data exists for this cell, regenerates from it using the
 * diff algorithm to handle population/context changes.
 *
 * @param neighbors — optional NeighborGrid for seamless connectivity.
 */
export function generateSubworldMap(
	seed: number, width: number, height: number,
	mode: SubworldMode, parameter = 0,
	neighbors?: NeighborGrid,
): SubworldMapData & {mapData: MapData} {
	const fn = registry.get(mode);
	if (!fn) {
		throw new Error(`No generator registered for subworld mode "${mode}"`);
	}

	let mapData = fn(seed, width, height, parameter, neighbors);

	// Check for saved data and regenerate if available
	const saved = loadSubworldData(seed, mode);
	if (saved) {
		mapData = regenerateFromSaved(saved, mapData);
	}

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
		heightmap: mapData.heightmap,
		structures: mapData.structures,
		mapData,
	};
}

/** Data returned by the worker-compatible generator. */
export type WorkerGenResult = {
	/** Full MapData for main-thread rendering + saving. */
	mapData: MapData;
	/** Traversability grid (0=blocked, 1+=walkable). */
	traversability: Uint8Array;
};

/**
 * Worker-safe generation: produces raw MapData + traversability.
 * Rendering is done on the main thread using renderMap() to avoid
 * any OffscreenCanvas compatibility issues.
 * Accepts optional savedData since the worker has no access to the
 * main-thread save cache.
 */
export function generateSubworldMapForWorker(
	seed: number, width: number, height: number,
	mode: SubworldMode, parameter = 0,
	neighbors?: NeighborGrid,
	savedData?: SavedSubworldData,
): WorkerGenResult {
	const fn = registry.get(mode);
	if (!fn) {
		throw new Error(`No generator registered for subworld mode "${mode}"`);
	}

	let mapData = fn(seed, width, height, parameter, neighbors);

	if (savedData) {
		mapData = regenerateFromSaved(savedData, mapData);
	}

	const traversability = generateTraversability(mapData);

	return {mapData, traversability};
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

/**
 * Create a SavedSubworldData snapshot from current map data.
 * Call this when the player exits the subworld.
 */
export function createSubworldSnapshot(mapData: MapData): SavedSubworldData {
	// Quantise heightmap to Uint16 for compact storage
	const quantised = new Uint16Array(mapData.heightmap.length);
	for (let i = 0; i < mapData.heightmap.length; i++) {
		quantised[i] = Math.round(mapData.heightmap[i] * 65_535);
	}

	let maxId = 0;
	for (const s of mapData.structures) {
		if (s.id > maxId) {
			maxId = s.id;
		}
	}

	return {
		seed: mapData.seed,
		mode: mapData.mode,
		width: mapData.width,
		height: mapData.height,
		heightmap: quantised,
		structures: mapData.structures.map(s => ({...s})),
		nextStructureId: maxId + 1,
	};
}

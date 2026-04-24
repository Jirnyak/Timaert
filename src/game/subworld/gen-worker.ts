// === Subworld generation Web Worker ===
//
// Runs map generation AND rendering off the main thread.
// Returns raw MapData + traversability + pre-rendered ImageBitmap.

import {
	type SubworldMode, type CellContext, type NeighborGrid,
	type SavedSubworldData, type MapData,
	Biome,
} from './map-data';
import {generateSubworldMapForWorker} from './map-factory';
import {renderMapOffscreen} from './map-renderer';
import {WATER_LEVEL} from './base-generator';

// ── Message types ───────────────────────────────────────────────────

export type GenRequest = {
	type: 'generate';
	/** Unique key for this request (e.g. 'cx,cy'). */
	key: string;
	cx: number;
	cy: number;
	seed: number;
	width: number;
	height: number;
	mode: SubworldMode;
	parameter: number;
	/** 9 CellContext objects for neighbor grid. */
	neighborCells: CellContext[];
	/** Macroworld sea level for universal water plane. */
	seaLevel: number;
	centerX: number;
	centerY: number;
	/** Optional saved data for regeneration. */
	savedData?: SavedSubworldData;
};

export type GenResponse = {
	type: 'generated';
	key: string;
	cx: number;
	cy: number;
	mode: SubworldMode;
	seed: number;
	parameter: number;
	/** Raw generation output. */
	mapData: MapData;
	/** Traversability grid (0=blocked, 1+=walkable). */
	traversability: Uint8Array;
	/** Pre-rendered visual (rendered off-thread via OffscreenCanvas). */
	visual: ImageBitmap;
};

// ── Worker entry point ────────────────────────────────────────────────

globalThis.addEventListener('message', (event: MessageEvent<GenRequest>) => {
	const request = event.data;
	if (request.type !== 'generate') {
		return;
	}

	try {
		// Reconstruct NeighborGrid from flat cell array
		const neighbors: NeighborGrid = {
			cells: request.neighborCells as unknown as NeighborGrid['cells'],
			center: request.neighborCells[4],
			seaLevel: request.seaLevel,
		};

		const result = generateSubworldMapForWorker(request.seed, request.width, request.height, request.mode, request.parameter, neighbors, request.savedData);

		// Render visual off-thread via OffscreenCanvas → ImageBitmap (zero-copy transfer)
		const visual = renderMapOffscreen(result.mapData);

		const response: GenResponse = {
			type: 'generated',
			key: request.key,
			cx: request.cx,
			cy: request.cy,
			mode: request.mode,
			seed: request.seed,
			parameter: request.parameter,
			mapData: result.mapData,
			traversability: result.traversability,
			visual,
		};

		// Transfer the ImageBitmap + large typed-array buffers (zero-copy);
		// structured clone for the rest. Saves ~5 MB/cell of clone overhead.
		const transfer: Transferable[] = [visual];
		const seen = new Set<ArrayBuffer>();
		const addBuf = (b: ArrayBufferLike): void => {
			if (b instanceof ArrayBuffer && !seen.has(b)) {
				seen.add(b);
				transfer.push(b);
			}
		};

		addBuf(result.mapData.grid.buffer);
		addBuf(result.mapData.heightmap.buffer);
		addBuf(result.traversability.buffer);

		self.postMessage(response, {transfer});
	} catch (error) {
		console.error(`[gen-worker] FAILED key=${request.key} mode=${request.mode}`, error);
		const fallbackCanvas = new OffscreenCanvas(request.width * 2, request.height * 2);
		const fallbackVisual = fallbackCanvas.transferToImageBitmap();
		const errorResponse: GenResponse = {
			type: 'generated',
			key: request.key,
			cx: request.cx,
			cy: request.cy,
			mode: request.mode,
			seed: request.seed,
			parameter: request.parameter,
			mapData: {
				grid: new Uint8Array(request.width * request.height),
				width: request.width,
				height: request.height,
				spawnX: Math.floor(request.width / 2),
				spawnY: Math.floor(request.height / 2),
				seed: request.seed,
				mode: request.mode,
				houses: [],
				walls: [],
				fieldPlots: [],
				mainRoadPaths: [],
				streetNodes: [],
				streetEdges: [],
				heightmap: new Float32Array(request.width * request.height),
				structures: [],
				biome: Biome.Meadow,
				waterLevel: WATER_LEVEL,
			},
			traversability: new Uint8Array(request.width * request.height).fill(1),
			visual: fallbackVisual,
		};
		self.postMessage(errorResponse, {transfer: [fallbackVisual]});
	}
});

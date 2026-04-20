// === Seamless Subworld Manager — 9-cell infinite scrolling ===
//
// Maintains a 3×3 grid of generated subworld cells centred on the
// player's current macroworld position. When the player walks past
// a cell boundary the grid shifts incrementally: only the newly
// exposed cells are generated and stale ones are saved + freed.
//
// For the player the experience is a continuous, seamless world.

import {
	buildNeighborGrid,
	type SubworldMode, type SubworldMapData, type MapData,
	type CellContext, type Structure,
} from './map-data';
import {
	generateSubworldMap, saveSubworldData, loadSubworldData,
	createSubworldSnapshot,
} from './map-factory';
import type {GenRequest, GenResponse} from './gen-worker';
import {WATER_LEVEL} from './base-generator';

// ── Constants ───────────────────────────────────────────────────

/** Subworld tile size per cell. */
export const CELL_SIZE = 1024;
const FULL = CELL_SIZE * 3;

// ── Types ───────────────────────────────────────────────────────

/** A single loaded subworld cell. */
export type LoadedCell = {
	/** Macroworld coords. */
	cx: number;
	cy: number;
	/** Derived subworld mode. */
	mode: SubworldMode;
	/** Seed used for generation. */
	seed: number;
	/** Numeric parameter (population, etc.). */
	parameter: number;
	/** Generated map data. */
	mapData: MapData;
	/** Consumer-facing data (visual, traversability, heightmap, structures). */
	subData: SubworldMapData;
};

/**
 * Callback the host provides so the manager can resolve any
 * macroworld cell into a CellContext without importing macroworld code.
 */
export type CellResolver = (cx: number, cy: number) => CellContext;

/**
 * Determines the SubworldMode for a cell based on its features/landmarks.
 */
export type ModeResolver = (ctx: CellContext) => SubworldMode;

/** Result of a boundary-crossing shift. */
export type ShiftResult = {
	/** New center cell coords. */
	centerX: number;
	centerY: number;
	/** Cells that were loaded (the newly visible ones). */
	loaded: LoadedCell[];
	/** Cell keys that were unloaded (saved + freed). */
	unloaded: string[];
	/** Player position in the new coordinate frame. */
	playerX: number;
	playerY: number;
	/** The mode of the new center cell, for NPC re-spawning. */
	centerMode: SubworldMode;
};

// ── Manager ─────────────────────────────────────────────────────

export class SeamlessSubworldManager {
	/** Loaded 3×3 grid, keyed by 'cx,cy'. */
	private readonly cells = new Map<string, LoadedCell>();

	// Persistent composite buffers — reused across shifts to avoid allocation
	private readonly _travData = new Uint8Array(FULL * FULL);
	private readonly _tileData = new Uint8Array(FULL * FULL);
	private readonly _heightData = new Float32Array(FULL * FULL);
	private readonly _visCanvas: HTMLCanvasElement;
	private readonly _visCtx: CanvasRenderingContext2D;

	// Worker pool for off-thread cell generation
	private readonly workers: Worker[] = [];
	private workerIdx = 0;
	private readonly workerCallbacks = new Map<string, (cell: LoadedCell) => void>();

	/**
	 * Raw worker results waiting for main-thread renderMap.
	 * Drained at most 1 per frame in tickPreload to spread cost evenly.
	 */
	private readonly pendingResults: GenResponse[] = [];

	/** Cells waiting to be blitted into composites (1 per frame). */
	private readonly pendingBlits: LoadedCell[] = [];

	/** Cells waiting to be saved (1 per frame). */
	private readonly pendingSaves: LoadedCell[] = [];

	/** Set by checkBoundary — causes tickPreload to skip drains for one frame. */
	private _skipDrains = false;

	/**
	 * When true, worker results are queued and drained 1-per-frame.
	 * When false (initial load), results are finalized immediately.
	 */
	private deferRendering = false;

	/**
	 * Called after each incremental composite blit so the host can
	 * refresh engine data (traversability, 3D uploads, etc.).
	 * When `region` is provided, only that sub-rectangle was modified
	 * (single cell blit) — the host can do a partial GPU upload.
	 * When `region` is undefined, the entire composite was rebuilt
	 * (shift + multi-cell blit) — a full upload is required.
	 */
	onCompositesUpdated?: (region?: {ox: number; oy: number; w: number; h: number}) => void;

	/**
	 * Called when a cell has been blitted into composites and is ready
	 * for NPC spawning. Fires for both preloaded and async-generated cells.
	 */
	onCellReady?: (cell: LoadedCell) => void;

	constructor(
		public centerX: number,
		public centerY: number,
		private readonly resolveCell: CellResolver,
		private readonly resolveMode: ModeResolver,
		private readonly mapW: number,
		private readonly mapH: number,
		private readonly seaLevel = 0.4,
	) {
		this._visCanvas = document.createElement('canvas');
		this._visCanvas.width = FULL;
		this._visCanvas.height = FULL;
		this._visCtx = this._visCanvas.getContext('2d')!;

		// Spawn 2 workers for parallel cell generation
		for (let i = 0; i < 2; i++) {
			const w = new Worker(
				new URL('gen-worker.ts', import.meta.url),
				{type: 'module'},
			);
			w.addEventListener('message', (event: MessageEvent<GenResponse>) => {
				this.handleWorkerResult(event.data);
			});
			w.addEventListener('error', (event: ErrorEvent) => {
				console.error('[seamless] Worker error:', event.message, event);
			});
			this.workers.push(w);
		}
	}

	// ── Initialisation ──────────────────────────────────────────

	/**
	 * Generate all 9 cells for the initial grid.
	 * Call once when entering the subworld.
	 */
	generateAll(): void {
		this.forEachGridCell((cx, cy) => {
			this.placeCell(cx, cy);
		});
	}

	/**
	 * Async version: generate all 9 cells via Web Workers.
	 * Center cell resolves first, remaining 8 in parallel.
	 */
	async generateAllAsync(): Promise<void> {
		// During initial load, finalize results immediately (loading screen shown)
		this.deferRendering = false;

		// Center cell first — most important for immediate gameplay
		const cCX = this.wrap(this.centerX, this.mapW);
		const cCY = this.macroY(0);
		const center = await this.requestWorkerCell(cCX, cCY);
		this.cells.set(this.key(cCX, cCY), center);

		// Remaining 8 cells — fire all at once, workers process in parallel
		const promises: Array<Promise<void>> = [];
		for (let r = -1; r <= 1; r++) {
			for (let c = -1; c <= 1; c++) {
				if (r === 0 && c === 0) {
					continue;
				}

				const cx = this.wrap(this.centerX + c, this.mapW);
				const cy = this.macroY(r);
				promises.push(this.loadAndPlace(cx, cy));
			}
		}

		await Promise.all(promises);

		// Switch to deferred mode — from now on, renderMap is spread 1-per-frame
		this.deferRendering = true;

		// Eagerly preload the surrounding ring while the player is in the
		// center — by the time they reach any boundary everything is ready.
		this.preloadEntireRing();
	}

	// ── Pre-loading ─────────────────────────────────────────────

	/** Cells already being pre-generated (by key). */
	private readonly preloaded = new Map<string, LoadedCell>();
	private readonly generating = new Set<string>();

	/** Staggered preload queue — dispatches cells to workers. */
	private readonly preloadQueue: Array<[number, number]> = [];
	private readonly queued = new Set<string>();

	/** Margin in tiles from cell edge to trigger pre-load. */
	private static readonly PRELOAD_MARGIN = CELL_SIZE * 0.75;

	/**
	 * Call every frame with the player's global position.
	 * Schedules pre-generation if the player is near a cell edge.
	 */
	tickPreload(gx: number, gy: number): void {
		// On a shift frame checkBoundary already did heavy work —
		// skip drains to keep the frame budget tight.
		if (this._skipDrains) {
			this._skipDrains = false;
		} else {
			this.drainPendingResults();
			this.drainPendingBlits();
			this.drainPendingSaves();
		}

		const localX = gx - CELL_SIZE; // Local pos within center cell (center starts at CELL_SIZE)
		const localY = gy - CELL_SIZE;
		const margin = SeamlessSubworldManager.PRELOAD_MARGIN;

		// Determine which direction(s) the player approaches
		const goingEast = localX > CELL_SIZE - margin;
		const goingWest = localX < margin;
		const goingSouth = localY > CELL_SIZE - margin;
		const goingNorth = localY < margin;

		if (goingEast) {
			this.preloadStrip(1, 0);
		}

		if (goingWest) {
			this.preloadStrip(-1, 0);
		}

		if (goingSouth) {
			this.preloadStrip(0, 1);
		}

		if (goingNorth) {
			this.preloadStrip(0, -1);
		}

		// Diagonal preload for corner approaches
		if (goingEast && goingSouth) {
			this.preloadDiag(1, 1);
		}

		if (goingEast && goingNorth) {
			this.preloadDiag(1, -1);
		}

		if (goingWest && goingSouth) {
			this.preloadDiag(-1, 1);
		}

		if (goingWest && goingNorth) {
			this.preloadDiag(-1, -1);
		}

		// Drain the queue: dispatch at most 1 worker request per interval
		this.drainPreloadQueue();
	}

	/**
	 * Pre-generate one strip of 3 cells in the given direction from center.
	 * Queues cells for staggered dispatch via Web Worker.
	 */
	private preloadStrip(dx: number, dy: number): void {
		for (let i = -1; i <= 1; i++) {
			const colOff = dx * 2 + (dy === 0 ? 0 : i);
			const rowOff = dy * 2 + (dx === 0 ? 0 : i);
			const cx = this.wrap(this.centerX + colOff, this.mapW);
			const cy = this.macroY(rowOff);
			this.queuePreload(cx, cy);
		}
	}

	/** Queue a single diagonal corner cell for preloading. */
	private preloadDiag(dx: number, dy: number): void {
		const cx = this.wrap(this.centerX + dx * 2, this.mapW);
		const cy = this.macroY(dy * 2);
		this.queuePreload(cx, cy);
	}

	/** Add a cell to the preload queue (deduplicated). */
	private queuePreload(cx: number, cy: number): void {
		const k = this.key(cx, cy);
		if (this.cells.has(k) || this.preloaded.has(k)
			|| this.generating.has(k) || this.queued.has(k)) {
			return;
		}

		this.queued.add(k);
		this.preloadQueue.push([cx, cy]);
	}

	/**
	 * Dispatch at most 1 queued cell to a worker per call.
	 * Each requestWorkerCell runs ~11 resolveCell calls synchronously;
	 * limiting to 1 per frame keeps the per-frame budget tight.
	 */
	private drainPreloadQueue(): void {
		while (this.preloadQueue.length > 0) {
			const [cx, cy] = this.preloadQueue.shift()!;
			const k = this.key(cx, cy);
			this.queued.delete(k);

			if (this.cells.has(k) || this.preloaded.has(k) || this.generating.has(k)) {
				continue;
			}

			this.generating.add(k);
			void this.preloadAsync(k, cx, cy);
			return; // Max 1 dispatch per frame
		}
	}

	/**
	 * Eagerly preload the entire ring of 16 cells surrounding the current
	 * 3×3 grid. Workers process off-thread; renderMap is spread 1-per-frame
	 * via drainPendingResults. By the time the player reaches any boundary
	 * (~5 s walk), every cell they could need is already generated.
	 *
	 * queuePreload is idempotent — calling this multiple times is safe.
	 */
	private preloadEntireRing(): void {
		// Cardinal strips (3 cells × 4 directions = 12 cells)
		this.preloadStrip(1, 0);
		this.preloadStrip(-1, 0);
		this.preloadStrip(0, 1);
		this.preloadStrip(0, -1);
		// Diagonal corners (1 cell × 4 diagonals = 4 cells)
		this.preloadDiag(1, 1);
		this.preloadDiag(1, -1);
		this.preloadDiag(-1, 1);
		this.preloadDiag(-1, -1);
		// Cells are dispatched 1-per-frame via drainPreloadQueue in tickPreload
	}

	/**
	 * Generate a cell via worker. On completion, either promote it to the
	 * active grid (if the cell is needed by the current 3×3) or park it in
	 * the preloaded cache for future shifts.
	 */
	private async preloadAsync(k: string, cx: number, cy: number): Promise<void> {
		const cell = await this.requestWorkerCell(cx, cy);
		this.generating.delete(k);
		this.queued.delete(k);

		// If this cell is part of the current active grid, promote + queue blit
		const activeKeys = this.gridKeys(this.centerX, this.centerY);
		if (activeKeys.has(k) && !this.cells.has(k)) {
			this.cells.set(k, cell);
			this.pendingBlits.push(cell);
		} else if (!this.cells.has(k)) {
			this.preloaded.set(k, cell);
		}
	}

	/** Request a worker cell and place it in the active grid. */
	private async loadAndPlace(cx: number, cy: number): Promise<void> {
		const cell = await this.requestWorkerCell(cx, cy);
		this.cells.set(this.key(cx, cy), cell);
	}

	// ── Queries ─────────────────────────────────────────────────

	/** Get the loaded cell at macroworld (cx, cy), if loaded. */
	getCell(cx: number, cy: number): LoadedCell | undefined {
		return this.cells.get(this.key(cx, cy));
	}

	/** Get the center cell. */
	getCenter(): LoadedCell | undefined {
		return this.getCell(this.centerX, this.centerY);
	}

	/** Iterator over all loaded cells. */
	allCells(): IterableIterator<LoadedCell> {
		return this.cells.values();
	}

	/** Number of loaded cells. */
	get loadedCount(): number {
		return this.cells.size;
	}

	// ── Coordinate conversion ───────────────────────────────────

	/**
	 * Convert global subworld position to (macroCell, localX, localY).
	 * The global coordinate frame has (0,0) at the NW corner of the
	 * NW neighbour and extends to (CELL_SIZE*3, CELL_SIZE*3).
	 * The center cell occupies [CELL_SIZE..2*CELL_SIZE).
	 */
	globalToCell(gx: number, gy: number): {
		cx: number; cy: number; lx: number; ly: number;
	} {
		const col = Math.floor(gx / CELL_SIZE);
		const row = Math.floor(gy / CELL_SIZE);
		const lx = gx - col * CELL_SIZE;
		const ly = gy - row * CELL_SIZE;
		const cx = this.wrap(this.centerX + (col - 1), this.mapW);
		const cy = this.macroY(row - 1); // Y-inverted
		return {
			cx, cy, lx, ly,
		};
	}

	/**
	 * Convert (macroCell, localX, localY) to global subworld position.
	 */
	cellToGlobal(cx: number, cy: number, lx: number, ly: number): {gx: number; gy: number} {
		// Compute column offset from center (-1, 0, 1)
		let col = cx - this.centerX;
		// Row offset: Y-inverted (macro north = subworld top = row -1)
		let row = this.centerY - cy;
		// Torus wrapping
		if (col > this.mapW / 2) {
			col -= this.mapW;
		} else if (col < -this.mapW / 2) {
			col += this.mapW;
		}

		if (row > this.mapH / 2) {
			row -= this.mapH;
		} else if (row < -this.mapH / 2) {
			row += this.mapH;
		}

		const gx = (col + 1) * CELL_SIZE + lx;
		const gy = (row + 1) * CELL_SIZE + ly;
		return {gx, gy};
	}

	/**
	 * Player's global spawn point (center of center cell).
	 */
	spawnPoint(): {gx: number; gy: number} {
		const center = this.getCenter();
		const sx = center?.subData.spawnX ?? CELL_SIZE / 2;
		const sy = center?.subData.spawnY ?? CELL_SIZE / 2;
		return this.cellToGlobal(this.centerX, this.centerY, sx, sy);
	}

	// ── Boundary detection + shift ──────────────────────────────

	/**
	 * Check if the global position requires a grid shift.
	 * Call every tick with the player's current global position.
	 * Returns a ShiftResult if a shift occurred, or undefined if not needed.
	 */
	checkBoundary(gx: number, gy: number): ShiftResult | undefined {
		const col = Math.floor(gx / CELL_SIZE);
		const row = Math.floor(gy / CELL_SIZE);

		// Col/row in [0,2]; center is (1,1)
		const dx = col - 1;
		const dy = row - 1;

		if (dx === 0 && dy === 0) {
			return undefined;
		}

		// New center — X is same convention, Y is inverted
		const newCX = this.wrap(this.centerX + dx, this.mapW);
		const newCY = this.wrap(this.centerY - dy, this.mapH); // Y-inverted!

		// Determine which cells stay and which are new
		const oldKeys = this.gridKeys(this.centerX, this.centerY);
		const newKeys = this.gridKeys(newCX, newCY);

		// Queue stale cells for deferred save (1 per frame via tickPreload)
		const unloaded: string[] = [];
		for (const k of oldKeys) {
			if (!newKeys.has(k)) {
				const cell = this.cells.get(k);
				if (cell) {
					this.pendingSaves.push(cell);
				}

				this.cells.delete(k);
				unloaded.push(k);
			}
		}

		// Update center
		this.centerX = newCX;
		this.centerY = newCY;

		// Shift composite buffers in-place (fast memcpy, no per-cell work)
		this._skipDrains = true;
		this.shiftComposites(dx, dy);

		// Promote preloaded cells + blit immediately (no visible holes)
		const loaded: LoadedCell[] = [];
		let blittedAny = false;
		this.forEachGridCell((cx, cy) => {
			const k = this.key(cx, cy);
			if (this.cells.has(k)) {
				return;
			}

			const pre = this.preloaded.get(k);
			if (pre) {
				this.cells.set(k, pre);
				this.preloaded.delete(k);
				loaded.push(pre);
				// Blit immediately — cell is fully generated, no need to defer
				this.blitCellComposite(pre);
				this.onCellReady?.(pre);
				blittedAny = true;
			} else if (!this.generating.has(k)) {
				// Not preloaded — fire async worker. preloadAsync queues blit on completion.
				this.generating.add(k);
				void this.preloadAsync(k, cx, cy);
			}
		});

		if (blittedAny) {
			this.onCompositesUpdated?.();
		}

		// Prune stale preloaded cells far from new center
		for (const [k, cell] of this.preloaded) {
			const dcx = Math.abs(cell.cx - newCX);
			const dcy = Math.abs(cell.cy - newCY);
			const wrappedDcx = Math.min(dcx, this.mapW - dcx);
			const wrappedDcy = Math.min(dcy, this.mapH - dcy);
			if (wrappedDcx > 2 || wrappedDcy > 2) {
				this.preloaded.delete(k);
			}
		}

		// Eagerly preload the ring around the NEW center so the next
		// boundary crossing finds everything ready.
		this.preloadEntireRing();

		// Recompute player position in new coordinate frame
		const playerX = gx - dx * CELL_SIZE;
		const playerY = gy - dy * CELL_SIZE;

		return {
			centerX: newCX, centerY: newCY,
			loaded, unloaded, playerX, playerY,
			centerMode: this.getCenter()?.mode ?? 'grassland',
		};
	}

	// ── Incremental composite updates ───────────────────────────

	/**
	 * Shift all composite buffers in-place by (dx, dy) cells.
	 * Typed arrays use copyWithin (native memcpy). Canvas uses
	 * self-copy drawImage — one GPU blit versus 6 individual cell reblits.
	 */
	private shiftComposites(dx: number, dy: number): void {
		const px = -dx * CELL_SIZE;
		const py = -dy * CELL_SIZE;

		shiftTypedArray(this._travData, FULL, FULL, px, py);
		shiftTypedArray(this._tileData, FULL, FULL, px, py);
		shiftFloat32(this._heightData, FULL, FULL, px, py);

		// Canvas self-copy shift — single drawImage instead of 6× cell reblits
		this._visCtx.save();
		this._visCtx.globalCompositeOperation = 'copy';
		this._visCtx.drawImage(this._visCanvas, px, py);
		this._visCtx.restore();
		// Clear vacated strips where new cells will be blitted
		if (px > 0) {
			this._visCtx.clearRect(0, 0, px, FULL);
		} else if (px < 0) {
			this._visCtx.clearRect(FULL + px, 0, -px, FULL);
		}

		if (py > 0) {
			this._visCtx.clearRect(0, 0, FULL, py);
		} else if (py < 0) {
			this._visCtx.clearRect(0, FULL + py, FULL, -py);
		}
	}

	/**
	 * Blit a single loaded cell into the composite buffers at the correct position.
	 */
	private blitCellComposite(cell: LoadedCell): void {
		const off = this.cellGridOffset(cell.cx, cell.cy);
		if (!off) {
			return;
		}

		const ox = (off.c + 1) * CELL_SIZE;
		const oy = (off.r + 1) * CELL_SIZE;
		blitUint8(cell.subData.grid, ox, oy, this._travData);
		blitUint8(cell.subData.tileGrid, ox, oy, this._tileData);
		blitFloat32(cell.subData.heightmap, ox, oy, this._heightData);
		const vis = cell.subData.visual;
		this._visCtx.drawImage(vis, 0, 0, vis.width, vis.height, ox, oy, CELL_SIZE, CELL_SIZE);
	}

	/**
	 * Compute the (c, r) grid offset for a cell relative to the current center.
	 * Returns undefined if the cell is not part of the active 3×3 grid.
	 */
	private cellGridOffset(cx: number, cy: number): {c: number; r: number} | undefined {
		for (let r = -1; r <= 1; r++) {
			for (let c = -1; c <= 1; c++) {
				if (this.wrap(this.centerX + c, this.mapW) === cx
					&& this.macroY(r) === cy) {
					return {c, r};
				}
			}
		}

		return undefined;
	}

	/** Blit 1 pending cell per frame and notify host. */
	private drainPendingBlits(): void {
		if (this.pendingBlits.length === 0) {
			return;
		}

		const cell = this.pendingBlits.shift()!;
		const off = this.cellGridOffset(cell.cx, cell.cy);
		this.blitCellComposite(cell);
		this.onCellReady?.(cell);
		if (off) {
			const ox = (off.c + 1) * CELL_SIZE;
			const oy = (off.r + 1) * CELL_SIZE;
			this.onCompositesUpdated?.({
				ox, oy, w: CELL_SIZE, h: CELL_SIZE,
			});
		} else {
			this.onCompositesUpdated?.();
		}
	}

	/** Save 1 pending cell per frame (quantize heightmap + Map write). */
	private drainPendingSaves(): void {
		if (this.pendingSaves.length === 0) {
			return;
		}

		const cell = this.pendingSaves.shift()!;
		this.saveCell(cell);
	}

	// ── Composite data ──────────────────────────────────────────

	/**
	 * Rebuild all persistent composite buffers from the loaded 9 cells.
	 * Call after generateAll/generateAllAsync and after every shift.
	 */
	rebuildComposites(): void {
		this._travData.fill(0);
		this._tileData.fill(0);
		this._heightData.fill(0);
		this._visCtx.clearRect(0, 0, FULL, FULL);
		this.forEachGridOffset((c, r, cx, cy) => {
			const cell = this.getCell(cx, cy);
			if (!cell) {
				return;
			}

			const ox = (c + 1) * CELL_SIZE;
			const oy = (r + 1) * CELL_SIZE;
			blitUint8(cell.subData.grid, ox, oy, this._travData);
			blitUint8(cell.subData.tileGrid, ox, oy, this._tileData);
			blitFloat32(cell.subData.heightmap, ox, oy, this._heightData);
			const vis = cell.subData.visual;
			this._visCtx.drawImage(vis, 0, 0, vis.width, vis.height, ox, oy, CELL_SIZE, CELL_SIZE);
		});
	}

	/** Return persistent traversability buffer (ref — do not reallocate). */
	compositeTraversability(): {width: number; height: number; data: Uint8Array} {
		return {width: FULL, height: FULL, data: this._travData};
	}

	/** Return persistent tile grid buffer (ref — do not reallocate). */
	compositeTileGrid(): {width: number; height: number; data: Uint8Array} {
		return {width: FULL, height: FULL, data: this._tileData};
	}

	/** Return persistent heightmap buffer (ref). */
	compositeHeightmap(): Float32Array {
		return this._heightData;
	}

	/** Return the water level (universal sea level). */
	compositeWaterLevel(): number {
		return this.getCenter()?.mapData.waterLevel ?? WATER_LEVEL;
	}

	/** Return persistent visual canvas (ref). */
	compositeVisual(): HTMLCanvasElement {
		return this._visCanvas;
	}

	/**
	 * Collect all structures from all 9 cells, offset to global coords.
	 */
	compositeStructures(): Structure[] {
		const result: Structure[] = [];
		this.forEachGridOffset((c, r, cx, cy) => {
			const cell = this.getCell(cx, cy);
			if (!cell) {
				return;
			}

			const ox = (c + 1) * CELL_SIZE;
			const oy = (r + 1) * CELL_SIZE;
			for (const s of cell.subData.structures) {
				result.push({...s, x: s.x + ox, y: s.y + oy});
			}
		});
		return result;
	}

	// ── Save/restore ────────────────────────────────────────────

	/** Save all loaded cells, clear state, and terminate workers. */
	saveAndClear(): void {
		// Flush any pending saves immediately
		for (const cell of this.pendingSaves) {
			this.saveCell(cell);
		}

		for (const cell of this.cells.values()) {
			this.saveCell(cell);
		}

		this.cells.clear();
		this.preloaded.clear();
		this.generating.clear();
		this.queued.clear();
		this.preloadQueue.length = 0;
		this.pendingResults.length = 0;
		this.pendingBlits.length = 0;
		this.pendingSaves.length = 0;
		this.workerCallbacks.clear();

		for (const w of this.workers) {
			w.terminate();
		}
	}

	// ── Internals ───────────────────────────────────────────────

	/**
	 * Convert subworld grid row offset (-1/0/+1) to macro Y coordinate.
	 * Inverts Y: subworld north (r=-1) → macro centerY+1.
	 */
	private macroY(r: number): number {
		return this.wrap(this.centerY - r, this.mapH);
	}

	/** Iterate all 9 grid cells in row-major order, yielding macro coords. */
	private forEachGridCell(fn: (cx: number, cy: number) => void): void {
		for (let r = -1; r <= 1; r++) {
			for (let c = -1; c <= 1; c++) {
				fn(this.wrap(this.centerX + c, this.mapW), this.macroY(r));
			}
		}
	}

	/** Iterate all 9 grid cells, yielding column/row offsets + macro coords. */
	private forEachGridOffset(fn: (c: number, r: number, cx: number, cy: number) => void): void {
		for (let r = -1; r <= 1; r++) {
			for (let c = -1; c <= 1; c++) {
				fn(c, r, this.wrap(this.centerX + c, this.mapW), this.macroY(r));
			}
		}
	}

	/** Get all cell keys for a 3×3 grid centred on (cx, cy). */
	private gridKeys(cx: number, cy: number): Set<string> {
		const keys = new Set<string>();
		for (let r = -1; r <= 1; r++) {
			for (let c = -1; c <= 1; c++) {
				keys.add(this.key(
					this.wrap(cx + c, this.mapW),
					this.wrap(cy - r, this.mapH), // Y-inverted
				));
			}
		}

		return keys;
	}

	/** Build a cell and place it in the active grid. */
	private placeCell(cx: number, cy: number): LoadedCell {
		const cell = this.buildCell(cx, cy);
		this.cells.set(this.key(cx, cy), cell);
		return cell;
	}

	/** Build a cell (pure — does not modify grid state). */
	private buildCell(cx: number, cy: number): LoadedCell {
		const ctx = this.resolveCell(cx, cy);
		const mode = this.resolveMode(ctx);
		// Flip Y for neighbor resolution: buildNeighborGrid row 0 = cy-1,
		// but macro north = cy+1.  Negate the Y offset.
		const flipResolve: CellResolver = (rx, ry) => this.resolveCell(rx, 2 * cy - ry);
		const grid = buildNeighborGrid(cx, cy, flipResolve, this.seaLevel);
		const result = generateSubworldMap(ctx.seed, CELL_SIZE, CELL_SIZE, mode, ctx.landmarkParam, grid);
		return {
			cx, cy, mode, seed: ctx.seed,
			parameter: ctx.landmarkParam,
			mapData: result.mapData,
			subData: {
				visual: result.visual,
				grid: result.grid,
				tileGrid: result.tileGrid,
				width: result.width,
				height: result.height,
				spawnX: result.spawnX,
				spawnY: result.spawnY,
				heightmap: result.heightmap,
				structures: result.structures,
			},
		};
	}

	private key(cx: number, cy: number): string {
		return `${cx},${cy}`;
	}

	private wrap(v: number, size: number): number {
		return ((v % size) + size) % size;
	}

	// ── Worker communication ────────────────────────────────────

	/** Round-robin worker selection. */
	private nextWorker(): Worker {
		const w = this.workers[this.workerIdx];
		this.workerIdx = (this.workerIdx + 1) % this.workers.length;
		return w;
	}

	/**
	 * Queue or immediately finalize a worker result.
	 * During gameplay (deferRendering=true): queued → 1 per frame.
	 * During initial load (deferRendering=false): processed immediately.
	 */
	private handleWorkerResult(resp: GenResponse): void {
		if (!this.workerCallbacks.has(resp.key)) {
			return;
		}

		if (this.deferRendering) {
			this.pendingResults.push(resp);
		} else {
			this.finalizePendingResult(resp);
		}
	}

	/**
	 * Finalize one pending result: resolve the callback with the
	 * pre-rendered visual from the worker (no main-thread renderMap).
	 * Called from drainPendingResults (max 1 per frame) or flush mode.
	 */
	private finalizePendingResult(resp: GenResponse): void {
		const cb = this.workerCallbacks.get(resp.key);
		if (!cb) {
			return;
		}

		this.workerCallbacks.delete(resp.key);

		const {mapData, visual} = resp;

		const cell: LoadedCell = {
			cx: resp.cx, cy: resp.cy,
			mode: resp.mode, seed: resp.seed,
			parameter: resp.parameter,
			mapData,
			subData: {
				visual,
				grid: resp.traversability,
				tileGrid: mapData.grid,
				width: mapData.width,
				height: mapData.height,
				spawnX: mapData.spawnX,
				spawnY: mapData.spawnY,
				heightmap: mapData.heightmap,
				structures: mapData.structures,
			},
		};
		cb(cell);
	}

	/**
	 * Process at most 1 pending worker result per call.
	 * Called every frame from tickPreload — spreads renderMap cost evenly.
	 */
	private drainPendingResults(): void {
		if (this.pendingResults.length === 0) {
			return;
		}

		const resp = this.pendingResults.shift()!;
		this.finalizePendingResult(resp);
	}

	/**
	 * Flush ALL pending results immediately (blocking).
	 * Used during initial load when a loading screen is shown.
	 */
	flushPendingResults(): void {
		while (this.pendingResults.length > 0) {
			this.finalizePendingResult(this.pendingResults.shift()!);
		}
	}

	/**
	 * Request a cell generation via Web Worker.
	 * Resolves cell data, builds neighbor grid, and sends to worker.
	 * Returns a promise that resolves when the worker finishes.
	 */
	private async requestWorkerCell(cx: number, cy: number): Promise<LoadedCell> {
		return new Promise(resolve => {
			const ctx = this.resolveCell(cx, cy);
			const mode = this.resolveMode(ctx);
			const flipResolve: CellResolver = (rx, ry) => this.resolveCell(rx, 2 * cy - ry);
			const neighbors = buildNeighborGrid(cx, cy, flipResolve, this.seaLevel);
			const saved = loadSubworldData(ctx.seed, mode);

			const k = this.key(cx, cy);
			this.workerCallbacks.set(k, resolve);

			const request: GenRequest = {
				type: 'generate',
				key: k,
				cx, cy,
				seed: ctx.seed,
				width: CELL_SIZE,
				height: CELL_SIZE,
				mode,
				parameter: ctx.landmarkParam,
				neighborCells: [...neighbors.cells],
				seaLevel: this.seaLevel,
				centerX: cx, centerY: cy,
				savedData: saved ?? undefined,
			};
			this.nextWorker().postMessage(request);
		});
	}

	/** Save a single cell to the subworld cache. */
	private saveCell(cell: LoadedCell): void {
		saveSubworldData(cell.seed, cell.mode, createSubworldSnapshot(cell.mapData));
	}
}

// ── Buffer helpers ──────────────────────────────────────────────

/** Copy a CELL_SIZE×CELL_SIZE Uint8Array into a FULL-width buffer at (ox,oy). */
function blitUint8(src: Uint8Array, ox: number, oy: number, dst: Uint8Array): void {
	for (let y = 0; y < CELL_SIZE; y++) {
		const dstOff = (oy + y) * FULL + ox;
		const srcOff = y * CELL_SIZE;
		dst.set(src.subarray(srcOff, srcOff + CELL_SIZE), dstOff);
	}
}

/** Copy a CELL_SIZE×CELL_SIZE Float32Array into a FULL-width buffer at (ox,oy). */
function blitFloat32(src: Float32Array, ox: number, oy: number, dst: Float32Array): void {
	for (let y = 0; y < CELL_SIZE; y++) {
		const dstOff = (oy + y) * FULL + ox;
		const srcOff = y * CELL_SIZE;
		dst.set(src.subarray(srcOff, srcOff + CELL_SIZE), dstOff);
	}
}

/**
 * Shift a Uint8Array grid (w×h) in-place by (sx, sy) pixels.
 * Positive sx = shift content right, positive sy = shift content down.
 * Vacated strips are zeroed.
 */
function shiftTypedArray(buf: Uint8Array, w: number, h: number, sx: number, sy: number): void {
	const absY = Math.abs(sy);

	// Vertical shift first (bulk row move)
	if (sy < 0) {
		buf.copyWithin(0, absY * w);
		buf.fill(0, (h - absY) * w);
	} else if (sy > 0) {
		buf.copyWithin(absY * w, 0, (h - absY) * w);
		buf.fill(0, 0, absY * w);
	}

	// Horizontal shift (per-row)
	if (sx !== 0) {
		const absX = Math.abs(sx);
		for (let y = 0; y < h; y++) {
			const r = y * w;
			if (sx < 0) {
				buf.copyWithin(r, r + absX, r + w);
				buf.fill(0, r + w - absX, r + w);
			} else {
				buf.copyWithin(r + absX, r, r + w - absX);
				buf.fill(0, r, r + absX);
			}
		}
	}
}

/**
 * Same as shiftTypedArray but for Float32Array.
 */
function shiftFloat32(buf: Float32Array, w: number, h: number, sx: number, sy: number): void {
	const absY = Math.abs(sy);

	if (sy < 0) {
		buf.copyWithin(0, absY * w);
		buf.fill(0, (h - absY) * w);
	} else if (sy > 0) {
		buf.copyWithin(absY * w, 0, (h - absY) * w);
		buf.fill(0, 0, absY * w);
	}

	if (sx !== 0) {
		const absX = Math.abs(sx);
		for (let y = 0; y < h; y++) {
			const r = y * w;
			if (sx < 0) {
				buf.copyWithin(r, r + absX, r + w);
				buf.fill(0, r + w - absX, r + w);
			} else {
				buf.copyWithin(r + absX, r, r + w - absX);
				buf.fill(0, r, r + absX);
			}
		}
	}
}

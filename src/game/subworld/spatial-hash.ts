/**
 * Subworld spatial hash — uniform grid for fast radius queries.
 *
 * Used by every combat/AI system to answer one universal question:
 *   "Which live npc/player entities are within R units of (x, y)?"
 *
 * Detection is purely local-radius — there are no cell boundaries,
 * no faction zones, no fights-of-war. Every entity sees the same
 * world the same way: a circle around itself.
 *
 * The grid is a simple uniform hash sized so each NPC touches ~1 cell.
 * Build O(N), query O(K) with K = entities in the queried radius.
 */

import type {SubworldEntity} from './types';

/** Cell size of the spatial hash (world units). Tuned so each cell holds a handful of entities. */
const CELL = 64;

export type SpatialHash = {
	cell: number;
	cols: number;
	rows: number;
	buckets: SubworldEntity[][];
};

function bucketIndex(hash: SpatialHash, x: number, y: number): number {
	const cx = Math.min(hash.cols - 1, Math.max(0, Math.floor(x / hash.cell)));
	const cy = Math.min(hash.rows - 1, Math.max(0, Math.floor(y / hash.cell)));
	return cy * hash.cols + cx;
}

export function buildSpatialHash(
	entities: readonly SubworldEntity[],
	worldWidth: number,
	worldHeight: number,
): SpatialHash {
	const cols = Math.max(1, Math.ceil(worldWidth / CELL));
	const rows = Math.max(1, Math.ceil(worldHeight / CELL));
	const buckets: SubworldEntity[][] = Array.from({length: cols * rows}, () => []);
	const hash: SpatialHash = {
		cell: CELL, cols, rows, buckets,
	};
	for (const entity of entities) {
		if (entity.kind !== 'npc' && entity.kind !== 'player') {
			continue;
		}

		if ((entity.hp ?? 0) <= 0) {
			continue;
		}

		buckets[bucketIndex(hash, entity.x, entity.y)].push(entity);
	}

	return hash;
}

/**
 * Visit every live npc/player entity within `radius` of (x, y).
 * Visitor is called once per candidate; caller does precise distance test
 * if it needs exact circle inclusion (we already cull to bucket overlap).
 */
export function forEachInRadius(
	hash: SpatialHash,
	x: number, y: number, radius: number,
	visit: (entity: SubworldEntity) => void,
): void {
	const r = Math.max(0, radius);
	const minCx = Math.max(0, Math.floor((x - r) / hash.cell));
	const maxCx = Math.min(hash.cols - 1, Math.floor((x + r) / hash.cell));
	const minCy = Math.max(0, Math.floor((y - r) / hash.cell));
	const maxCy = Math.min(hash.rows - 1, Math.floor((y + r) / hash.cell));
	const r2 = r * r;
	for (let cy = minCy; cy <= maxCy; cy++) {
		const rowBase = cy * hash.cols;
		for (let cx = minCx; cx <= maxCx; cx++) {
			for (const ent of hash.buckets[rowBase + cx]) {
				const dx = ent.x - x;
				const dy = ent.y - y;
				if (dx * dx + dy * dy <= r2) {
					visit(ent);
				}
			}
		}
	}
}

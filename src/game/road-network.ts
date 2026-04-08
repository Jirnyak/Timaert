// === Road Network — cell-level road tracing between landmarks ===
//
// Layer 1 (Macroworld). Feature type: Road.
//
// After world generation the GPU mask pipeline creates thick road
// corridors with domain warping for terrain flattening. This module
// traces *actual* 1-cell-width roads along those corridors to produce
// the visible road network on the feature layer.
//
// Algorithm — Corridor-guided Bresenham:
//   1. Reuse City.connections[] (MST + extras, already computed).
//   2. Walk a Bresenham line between each pair of connected landmarks.
//   3. At every step, snap to the nearby cell (±R) with the highest
//      GPU roadData value — this follows the corridor's natural curves.
//   4. Stamp snapped positions into a 1-cell-width road mask.
//
// Complexity: O(total_road_length × (2R+1)²) — instant for ~200 edges.

import type {TerrainData} from '../webgl/map-generator';

// ── Public API ──────────────────────────────────────────────────

export type Landmark = {
	readonly x: number;
	readonly y: number;
};

/**
 * Generate a 1-cell-width road network.
 * `edges` are index pairs into `landmarks` (pre-computed connectivity).
 * Returns a byte mask (width × height), 255 = road cell, 0 = no road.
 */
export function generateRoadNetwork(
	tData: TerrainData,
	landmarks: readonly Landmark[],
	edges: ReadonlyArray<readonly [number, number]>,
): Uint8Array {
	const {width: w, height: h} = tData;
	const roadMask = new Uint8Array(w * h);

	if (landmarks.length < 2 || edges.length === 0) {
		return roadMask;
	}

	for (const [a, b] of edges) {
		traceRoad(landmarks[a].x, landmarks[a].y, landmarks[b].x, landmarks[b].y, tData, roadMask, w, h);
	}

	return roadMask;
}

// ── Corridor-guided Bresenham trace ─────────────────────────────

const SNAP_RADIUS = 3;

function wrap(v: number, max: number): number {
	return ((v % max) + max) % max;
}

/**
 * Progressive corridor-guided trace from (ax,ay) to (bx,by) on a torus.
 * Instead of snapping each Bresenham point independently (which can jump
 * and break connectivity), each step picks the best corridor cell among
 * the 8-neighbors of the *previous* snapped position — guaranteeing
 * 8-connectivity by construction while still following GPU corridors.
 */
function traceRoad(
	ax: number, ay: number, bx: number, by: number,
	tData: TerrainData, mask: Uint8Array,
	w: number, h: number,
): void {
	// Torus-aware direction: pick shortest wrap
	let dx = bx - ax;
	let dy = by - ay;
	if (Math.abs(dx) > w / 2) {
		dx += dx > 0 ? -w : w;
	}

	if (Math.abs(dy) > h / 2) {
		dy += dy > 0 ? -h : h;
	}

	const steps = Math.max(Math.abs(dx), Math.abs(dy));
	if (steps === 0) {
		return;
	}

	// Anchor endpoints: roads must start/end exactly at landmarks
	let curX = ax;
	let curY = ay;
	mask[curY * w + curX] = 255;

	const endX = wrap(bx, w);
	const endY = wrap(by, h);

	// Walk: pick best corridor cell among 8-neighbors of current pos,
	// constrained to ±SNAP_RADIUS of the Bresenham guide point.
	// Near endpoints, tighten the leash so roads converge on landmarks.
	for (let i = 1; i <= steps; i++) {
		const t = i / steps;
		const baseX = wrap(Math.round(ax + dx * t), w);
		const baseY = wrap(Math.round(ay + dy * t), h);

		// Tighten snap near start/end so road meets landmarks cleanly
		const endDist = Math.min(i, steps - i);
		const radius = Math.max(1, Math.min(endDist, SNAP_RADIUS));

		let bestX = curX;
		let bestY = curY;
		let bestValue = -1;

		for (let oy = -1; oy <= 1; oy++) {
			for (let ox = -1; ox <= 1; ox++) {
				const nx = wrap(curX + ox, w);
				const ny = wrap(curY + oy, h);

				// Must be within radius of the Bresenham guide point
				let ddx = nx - baseX;
				let ddy = ny - baseY;
				if (Math.abs(ddx) > w / 2) {
					ddx += ddx > 0 ? -w : w;
				}

				if (Math.abs(ddy) > h / 2) {
					ddy += ddy > 0 ? -h : h;
				}

				if (Math.abs(ddx) > radius || Math.abs(ddy) > radius) {
					continue;
				}

				const value = tData.roadData[ny * w + nx];
				if (value > bestValue) {
					bestValue = value;
					bestX = nx;
					bestY = ny;
				}
			}
		}

		// Fallback: no 8-neighbor in snap range — step toward guide
		if (bestValue < 0) {
			let ddx = baseX - curX;
			let ddy = baseY - curY;
			if (Math.abs(ddx) > w / 2) {
				ddx += ddx > 0 ? -w : w;
			}

			if (Math.abs(ddy) > h / 2) {
				ddy += ddy > 0 ? -h : h;
			}

			bestX = wrap(curX + Math.sign(ddx), w);
			bestY = wrap(curY + Math.sign(ddy), h);
		}

		mask[bestY * w + bestX] = 255;
		curX = bestX;
		curY = bestY;
	}

	// Bridge any remaining gap to the destination landmark
	let gx = endX - curX;
	let gy = endY - curY;
	if (Math.abs(gx) > w / 2) {
		gx += gx > 0 ? -w : w;
	}

	if (Math.abs(gy) > h / 2) {
		gy += gy > 0 ? -h : h;
	}

	const gapSteps = Math.max(Math.abs(gx), Math.abs(gy));
	for (let j = 1; j <= gapSteps; j++) {
		const gt = j / gapSteps;
		const fx = wrap(Math.round(curX + gx * gt), w);
		const fy = wrap(Math.round(curY + gy * gt), h);
		mask[fy * w + fx] = 255;
	}
}

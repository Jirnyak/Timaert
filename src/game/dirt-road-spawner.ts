// === Dirt Road — narrow trails connecting villages to the road network ===
//
// Layer 1 (Macroworld). Feature type: DirtRoad.
//
// Two responsibilities:
//   1. Trace dirt-road paths from each village to the nearest main road cell.
//   2. Provide GLSL overlay for rendering dirt trails on the map.
//
// Dirt roads are narrower and more rustic than main roads — a simple
// earth-coloured trail without cobblestones or wheel tracks.

import type {Village} from './state';

// ── CPU: trace village → road connections ──────────────────────

/**
 * For each village, trace a dirt road to the nearest main-road cell.
 * Villages already ON a road cell get no dirt road (road takes priority).
 * Only stamps cells where `isLand` returns true — avoids water/ice.
 * Returns a byte mask (width × height), 255 = dirt road cell.
 */
export function generateDirtRoads(
	villages: readonly Village[],
	roadMask: Uint8Array,
	width: number,
	height: number,
	isLand?: (x: number, y: number) => boolean,
): Uint8Array {
	const dirtMask = new Uint8Array(width * height);

	for (const v of villages) {
		// Skip if village is already on a road
		const vi = v.y * width + v.x;
		if (roadMask[vi] > 0) {
			continue;
		}

		// BFS to find nearest road cell (max 60 tiles — villages are close to cities)
		const target = findNearestRoad_(v.x, v.y, roadMask, width, height, 60);
		if (!target) {
			continue;
		}

		// Bresenham trace from village to road cell
		traceBresenham_(v.x, v.y, target.x, target.y, dirtMask, roadMask, width, height, isLand);
	}

	return dirtMask;
}

/** BFS to find the nearest road cell within `maxDist` tiles. */
function findNearestRoad_(
	startX: number, startY: number,
	roadMask: Uint8Array,
	width: number, height: number,
	maxDist: number,
): {x: number; y: number} | undefined {
	// Spiral scan: cheaper than full BFS for small radii
	for (let r = 1; r <= maxDist; r++) {
		for (let dy = -r; dy <= r; dy++) {
			for (let dx = -r; dx <= r; dx++) {
				if (Math.abs(dx) !== r && Math.abs(dy) !== r) {
					continue; // Only check perimeter of current ring
				}

				const nx = ((startX + dx) % width + width) % width;
				const ny = ((startY + dy) % height + height) % height;
				if (roadMask[ny * width + nx] > 0) {
					return {x: nx, y: ny};
				}
			}
		}
	}

	return undefined;
}

/** Torus-aware Bresenham line, stamping dirt road cells. Stops at road cells. Skips non-land cells. */
function traceBresenham_(
	ax: number, ay: number, bx: number, by: number,
	dirtMask: Uint8Array, roadMask: Uint8Array,
	width: number, height: number,
	isLand?: (x: number, y: number) => boolean,
): void {
	let dx = bx - ax;
	let dy = by - ay;
	// Torus shortest path
	if (Math.abs(dx) > width / 2) {
		dx += dx > 0 ? -width : width;
	}

	if (Math.abs(dy) > height / 2) {
		dy += dy > 0 ? -height : height;
	}

	const steps = Math.max(Math.abs(dx), Math.abs(dy));
	if (steps === 0) {
		return;
	}

	for (let i = 0; i <= steps; i++) {
		const t = i / steps;
		const x = ((ax + Math.round(dx * t)) % width + width) % width;
		const y = ((ay + Math.round(dy * t)) % height + height) % height;
		const idx = y * width + x;
		// Don't overwrite existing road cells
		if (roadMask[idx] > 0) {
			continue;
		}

		// Skip water/ice cells
		if (isLand && !isLand(x, y)) {
			continue;
		}

		dirtMask[idx] = 255;
	}
}

// ── GLSL: dirt trail overlay ────────────────────────────────────

export const DIRT_ROAD_MAP_GLSL = /* glsl */ `
// Check if a neighbouring cell is a dirt road or main road (torus-safe)
bool dirtRoadAt(vec2 cell) {
	vec2 uv = mod(cell + 0.5, u_mapSize) / u_mapSize;
	float fid = texture(u_featureMap, uv).r * 255.0;
	// Connect to both dirt roads (4) and main roads (1)
	return (fid > 3.5 && fid < 4.5) || (fid > 0.5 && fid < 1.5);
}

vec3 dirtRoadOverlay(vec2 mapUV, vec3 baseColor) {
	vec2 pixelCoord = mapUV * u_mapSize;
	vec2 cell = floor(pixelCoord);
	vec2 cellUV = (cell + 0.5) / u_mapSize;

	float featureId = texture(u_featureMap, cellUV).r * 255.0;
	if (featureId < 3.5 || featureId > 4.5) return baseColor;

	// 16x16 virtual pixel grid
	vec2 p = floor(fract(pixelCoord) * 16.0) + 0.5;
	vec2 ctr = vec2(8.0);

	// Min distance to trail centreline across connections
	float md = 999.0;
	bool connected = false;

	// Cardinal neighbours
	if (dirtRoadAt(cell + vec2(0, -1))) { md = min(md, roadLineDist(p, ctr, vec2(8.0, 0.0))); connected = true; }
	if (dirtRoadAt(cell + vec2(0,  1))) { md = min(md, roadLineDist(p, ctr, vec2(8.0, 16.0))); connected = true; }
	if (dirtRoadAt(cell + vec2(1,  0))) { md = min(md, roadLineDist(p, ctr, vec2(16.0, 8.0))); connected = true; }
	if (dirtRoadAt(cell + vec2(-1, 0))) { md = min(md, roadLineDist(p, ctr, vec2(0.0, 8.0))); connected = true; }

	// Diagonal neighbours
	if (dirtRoadAt(cell + vec2(1, -1)))  { md = min(md, roadLineDist(p, ctr, vec2(16.0, 0.0))); connected = true; }
	if (dirtRoadAt(cell + vec2(-1, -1))) { md = min(md, roadLineDist(p, ctr, vec2(0.0, 0.0))); connected = true; }
	if (dirtRoadAt(cell + vec2(1, 1)))   { md = min(md, roadLineDist(p, ctr, vec2(16.0, 16.0))); connected = true; }
	if (dirtRoadAt(cell + vec2(-1, 1)))  { md = min(md, roadLineDist(p, ctr, vec2(0.0, 16.0))); connected = true; }

	if (!connected) md = length(p - ctr);

	// Narrow trail — 1.5 virtual pixels wide
	float hw = 1.5;
	if (md > hw) return baseColor;

	// Simple earth tones
	float cs = cell.x * 127.1 + cell.y * 311.7 + u_worldSeed;
	float ph = roadHash(cs + p.x * 17.31 + p.y * 43.77);
	float edge = smoothstep(hw - 0.8, hw, md);

	vec3 dirt = ph < 0.5
		? vec3(0.50, 0.40, 0.28)
		: vec3(0.46, 0.37, 0.25);

	// Slight grass bleed at edges
	vec3 grass = vec3(0.35, 0.50, 0.25);
	dirt = mix(dirt, grass, edge * 0.4);

	return mix(dirt, baseColor, edge * 0.3);
}
`;

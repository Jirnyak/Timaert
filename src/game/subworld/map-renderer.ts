// === Subworld map renderer — takes MapData, returns a visual canvas ===

import {
	TILE_ROAD, TILE_SQUARE, TILE_GRASS, TILE_FIELD,
	type MapData, type WallRing,
	isGateAngle,
} from './map-data';
import {getWallSegments} from './base-generator';

const SCALE = 2;

/** Render a MapData tile map into an HTMLCanvasElement. */
export function renderMap(data: MapData): HTMLCanvasElement {
	const {width, height, mode} = data;
	const urban = mode === 'city' || mode === 'village';

	const c = document.createElement('canvas');
	c.width = width * SCALE;
	c.height = height * SCALE;
	const ctx = c.getContext('2d')!;
	ctx.imageSmoothingEnabled = false;

	// 1. Background
	ctx.fillStyle = urban ? 'rgb(230, 220, 200)' : 'rgb(34, 54, 24)';
	ctx.fillRect(0, 0, c.width, c.height);

	// 2. Ground tiles
	drawGroundTiles(ctx, data, urban);

	// 3. Field overlays (urban only)
	if (urban) {
		drawFieldOverlays(ctx, data);
	}

	// 4. Street overlays
	drawStreetOverlays(ctx, data, urban);

	// 5. Walls & towers
	drawWalls(ctx, data, urban);

	// 6. Houses / trees
	drawHouses(ctx, data, urban);

	return c;
}

// ── Ground tiles (single pass) ──────────────────────────────────

function drawGroundTiles(ctx: CanvasRenderingContext2D, data: MapData, urban: boolean): void {
	const {grid, width, height, walls} = data;
	const roadColor1 = urban ? '#7a7056' : '#422e1a';
	const roadColor2 = urban ? '#857a5e' : '#4d3726';
	const squareColor = '#bebebe';
	const grassColor1 = '#7b8f57';
	const grassColor2 = '#6f8450';
	const fieldColor1 = '#b78f55';
	const fieldColor2 = '#a67d47';
	const outerTerrain1 = '#756248';
	const outerTerrain2 = '#5f6e4b';
	const outerWall = urban ? walls.at(-1) : undefined;

	for (let y = 0; y < height; y++) {
		for (let x = 0; x < width; x++) {
			const t = grid[(y * width) + x];
			switch (t) {
				case TILE_ROAD: {
					ctx.fillStyle = (x + y) % 2 === 0 ? roadColor1 : roadColor2;
					ctx.fillRect(x * SCALE, y * SCALE, SCALE, SCALE);
					break;
				}

				case TILE_SQUARE: {
					ctx.fillStyle = squareColor;
					ctx.fillRect(x * SCALE, y * SCALE, SCALE, SCALE);
					break;
				}

				case TILE_GRASS: {
					ctx.fillStyle = (x + y) % 3 === 0 ? grassColor1 : grassColor2;
					ctx.fillRect(x * SCALE, y * SCALE, SCALE, SCALE);
					break;
				}

				case TILE_FIELD: {
					ctx.fillStyle = x % 2 === 0 ? fieldColor1 : fieldColor2;
					ctx.fillRect(x * SCALE, y * SCALE, SCALE, SCALE);
					break;
				}

				default: {
					if (outerWall && !isInsideWallFast(outerWall, x + 0.5, y + 0.5)) {
						const noise = terrainNoise(x, y, data.seed);
						ctx.fillStyle = noise > 0.52 ? outerTerrain1 : outerTerrain2;
						ctx.fillRect(x * SCALE, y * SCALE, SCALE, SCALE);
					}

					break;
				}
			}
		}
	}
}

// ── Field overlays ──────────────────────────────────────────────

function drawFieldOverlays(ctx: CanvasRenderingContext2D, data: MapData): void {
	ctx.lineWidth = 0.5 * SCALE;
	for (const field of data.fieldPlots) {
		ctx.save();
		ctx.translate(field.x * SCALE, field.y * SCALE);
		ctx.rotate(field.rotation);
		ctx.fillStyle = 'rgba(188, 154, 92, 0.15)';
		ctx.fillRect((-field.w * SCALE) / 2, (-field.h * SCALE) / 2, field.w * SCALE, field.h * SCALE);
		ctx.strokeStyle = 'rgba(77, 56, 29, 0.25)';
		ctx.strokeRect((-field.w * SCALE) / 2, (-field.h * SCALE) / 2, field.w * SCALE, field.h * SCALE);
		ctx.restore();
	}
}

// ── Street / path overlays ──────────────────────────────────────

function drawStreetOverlays(ctx: CanvasRenderingContext2D, data: MapData, urban: boolean): void {
	ctx.lineCap = 'round';
	ctx.lineJoin = 'round';

	// Regular paths
	ctx.lineWidth = Number(SCALE);
	ctx.strokeStyle = urban ? 'rgb(170, 170, 170)' : 'rgb(65, 45, 30)';
	ctx.beginPath();
	for (const streetEdge of data.streetEdges) {
		const n1 = data.streetNodes[streetEdge.p1];
		const n2 = data.streetNodes[streetEdge.p2];
		if (!n1.isMain && !n2.isMain) {
			ctx.moveTo(n1.x * SCALE, n1.y * SCALE);
			ctx.lineTo(n2.x * SCALE, n2.y * SCALE);
		}
	}

	ctx.stroke();

	// Main roads — wider overlay following actual road centerlines
	ctx.lineWidth = 3 * SCALE;
	ctx.strokeStyle = urban ? 'rgba(120, 120, 120, 0.6)' : 'rgb(85, 60, 40)';
	const pathStep = Math.max(1, Math.floor(4 / SCALE));
	for (const path of data.mainRoadPaths) {
		ctx.beginPath();
		ctx.moveTo(path[0].x * SCALE, path[0].y * SCALE);
		for (let pi = pathStep; pi < path.length; pi += pathStep) {
			ctx.lineTo(path[pi].x * SCALE, path[pi].y * SCALE);
		}

		ctx.lineTo(path.at(-1)!.x * SCALE, path.at(-1)!.y * SCALE);
		ctx.stroke();
	}
}

// ── Walls & towers (with gate subdivision) ──────────────────────

function drawWalls(ctx: CanvasRenderingContext2D, data: MapData, urban: boolean): void {
	for (const wall of data.walls) {
		const segments = getWallSegments(wall);

		// Draw wall segments
		ctx.lineWidth = 2 * SCALE;
		ctx.strokeStyle = urban ? 'rgb(70, 70, 70)' : 'rgb(40, 40, 45)';
		ctx.beginPath();
		let hasAny = false;
		for (const seg of segments) {
			if (seg.isGate) {
				continue;
			}

			hasAny = true;
			ctx.moveTo(seg.p1.x * SCALE, seg.p1.y * SCALE);
			ctx.lineTo(seg.p2.x * SCALE, seg.p2.y * SCALE);
		}

		if (hasAny) {
			ctx.stroke();
		}

		// Draw towers at non-gate wall vertices
		const towerRadius = (urban ? 3 : 4) * SCALE;
		for (const p of wall.nodes) {
			const angle = Math.atan2(p.y - wall.centerY, p.x - wall.centerX);
			if (isGateAngle(wall, angle)) {
				continue;
			}

			ctx.fillStyle = urban ? 'rgb(90, 90, 90)' : 'rgb(50, 50, 55)';
			ctx.beginPath();
			ctx.arc(p.x * SCALE, p.y * SCALE, towerRadius, 0, Math.PI * 2);
			ctx.fill();
			if (urban) {
				ctx.fillStyle = 'rgb(110, 110, 110)';
				ctx.beginPath();
				ctx.arc(p.x * SCALE, p.y * SCALE, towerRadius * 0.7, 0, Math.PI * 2);
				ctx.fill();
			}
		}
	}
}

// ── Houses / trees ──────────────────────────────────────────────

function drawHouses(ctx: CanvasRenderingContext2D, data: MapData, urban: boolean): void {
	for (let i = 0; i < data.houses.length; i++) {
		const h = data.houses[i];
		const cx = (h.x + (h.w / 2)) * SCALE;
		const cy = (h.y + (h.h / 2)) * SCALE;
		const pw = h.w * SCALE;
		const ph = h.h * SCALE;

		ctx.save();
		ctx.translate(cx, cy);
		if (urban) {
			ctx.rotate(h.rotation);
			ctx.fillStyle = 'rgb(77, 55, 38)';
			ctx.fillRect(-pw / 2, -ph / 2, pw, ph);
			ctx.fillStyle = i % 10 < 7 ? 'rgb(143, 77, 54)' : 'rgb(122, 62, 41)';
			ctx.fillRect(-pw / 2, (-ph / 2) - 4, pw, ph);
			ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
			ctx.strokeRect(-pw / 2, (-ph / 2) - 4, pw, ph);
		} else {
			ctx.fillStyle = 'rgb(45, 90, 30)';
			ctx.beginPath();
			ctx.arc(0, -4, pw, 0, Math.PI * 2);
			ctx.fill();
			ctx.fillStyle = 'rgb(30, 60, 20)';
			ctx.beginPath();
			ctx.arc(0, 0, pw * 0.8, 0, Math.PI * 2);
			ctx.fill();
		}

		ctx.restore();
	}
}

// ── Helpers (duplicated from base to avoid circular import) ─────

function isInsideWallFast(wall: WallRing, x: number, y: number): boolean {
	const dx = x - wall.centerX;
	const dy = y - wall.centerY;
	const distance = Math.hypot(dx, dy);
	if (distance <= wall.avgRadius * 0.72) {
		return true;
	}

	if (distance >= wall.avgRadius * 1.35) {
		return false;
	}

	let inside = false;
	const points = wall.nodes;
	for (let i = 0, j = points.length - 1; i < points.length; j = i++) {
		const xi = points[i].x;
		const yi = points[i].y;
		const xj = points[j].x;
		const yj = points[j].y;
		const intersect = ((yi > y) !== (yj > y))
			&& (x < (((xj - xi) * (y - yi)) / ((yj - yi) || 0.000_01)) + xi);
		if (intersect) {
			inside = !inside;
		}
	}

	return inside;
}

function terrainNoise(x: number, y: number, worldSeed: number): number {
	let value = (x * 374_761_393) ^ (y * 668_265_263) ^ (worldSeed * 2_246_822_519);
	value = (value ^ (value >>> 13)) * 1_274_126_177;
	value ^= value >>> 16;
	return (value >>> 0) / 4_294_967_295;
}

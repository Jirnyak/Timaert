// === Subworld map renderer — takes MapData, returns a visual canvas ===

import {
	TILE_ROAD, TILE_SQUARE, TILE_GRASS, TILE_FIELD, TILE_TREE_DECOR,
	TILE_TUNDRA, TILE_TAIGA, TILE_SNOW, TILE_VALLEY,
	TILE_SWAMP, TILE_DESERT, TILE_STEPPE, TILE_TROPICS, TILE_WATER, TILE_SHORE, TILE_ROCK,
	type MapData, type WallRing,
	isGateAngle,
} from './map-data';
import {getWallSegments, getGateTowerPoints} from './base-generator';

export const RENDER_SCALE = 2;

type Ctx2D = CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;

/** Render a MapData tile map into an HTMLCanvasElement (main-thread). */
export function renderMap(data: MapData): HTMLCanvasElement {
	const {width, height} = data;
	const c = document.createElement('canvas');
	c.width = width * RENDER_SCALE;
	c.height = height * RENDER_SCALE;
	const ctx = c.getContext('2d')!;
	renderMapOnContext(ctx, data);
	return c;
}

/**
 * Render a MapData tile map into an ImageBitmap via OffscreenCanvas.
 * Safe to call from a Web Worker (no DOM dependency).
 */
export function renderMapOffscreen(data: MapData): ImageBitmap {
	const {width, height} = data;
	const c = new OffscreenCanvas(width * RENDER_SCALE, height * RENDER_SCALE);
	const ctx = c.getContext('2d')!;
	renderMapOnContext(ctx, data);
	return c.transferToImageBitmap();
}

/** Shared rendering logic for both Canvas and OffscreenCanvas contexts. */
function renderMapOnContext(ctx: Ctx2D, data: MapData): void {
	const {width, height, mode} = data;
	const urban = mode === 'city' || mode === 'village';

	ctx.imageSmoothingEnabled = false;

	// 1. Background — uniform for all cell types so boundaries are seamless
	ctx.fillStyle = 'rgb(34, 54, 24)';
	ctx.fillRect(0, 0, width * RENDER_SCALE, height * RENDER_SCALE);

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
}

// ── Ground tiles (single pass) ──────────────────────────────────

function drawGroundTiles(ctx: Ctx2D, data: MapData, urban: boolean): void {
	const {grid, width, height, walls} = data;
	const roadColor1 = urban ? '#7a7056' : '#422e1a';
	const roadColor2 = urban ? '#857a5e' : '#4d3726';
	const squareColor = '#bebebe';
	const fieldColor1 = '#b78f55';
	const fieldColor2 = '#a67d47';
	const outerTerrain1 = '#756248';
	const outerTerrain2 = '#5f6e4b';
	const outerWall = urban ? walls.at(-1) : undefined;

	// Biome ground color pairs [color1, color2]
	const biomeColors: Record<number, [string, string]> = {
		[TILE_TUNDRA]: ['#8a9a9a', '#7d8d8d'],
		[TILE_TAIGA]: ['#5a7050', '#4e6445'],
		[TILE_SNOW]: ['#c8d0d4', '#bcc4c8'],
		[TILE_VALLEY]: ['#6a8848', '#5e7c3e'],
		[TILE_GRASS]: ['#7b8f57', '#6f8450'],
		[TILE_SWAMP]: ['#4a5e3a', '#3e5230'],
		[TILE_DESERT]: ['#c4a854', '#b89c48'],
		[TILE_STEPPE]: ['#9a8850', '#8e7c44'],
		[TILE_TROPICS]: ['#3a8830', '#2e7c24'],
		[TILE_ROCK]: ['#6e6860', '#625c55'],
	};

	for (let y = 0; y < height; y++) {
		for (let x = 0; x < width; x++) {
			const t = grid[(y * width) + x];
			switch (t) {
				case TILE_ROAD: {
					ctx.fillStyle = (x + y) % 2 === 0 ? roadColor1 : roadColor2;
					ctx.fillRect(x * RENDER_SCALE, y * RENDER_SCALE, RENDER_SCALE, RENDER_SCALE);
					break;
				}

				case TILE_SQUARE: {
					ctx.fillStyle = squareColor;
					ctx.fillRect(x * RENDER_SCALE, y * RENDER_SCALE, RENDER_SCALE, RENDER_SCALE);
					break;
				}

				case TILE_FIELD: {
					ctx.fillStyle = x % 2 === 0 ? fieldColor1 : fieldColor2;
					ctx.fillRect(x * RENDER_SCALE, y * RENDER_SCALE, RENDER_SCALE, RENDER_SCALE);
					break;
				}

				case TILE_WATER: {
					ctx.fillStyle = (x + y) % 2 === 0 ? '#2a5a8a' : '#2e5e8e';
					ctx.fillRect(x * RENDER_SCALE, y * RENDER_SCALE, RENDER_SCALE, RENDER_SCALE);
					break;
				}

				case TILE_SHORE: {
					ctx.fillStyle = (x + y) % 2 === 0 ? '#c4a870' : '#b89e68';
					ctx.fillRect(x * RENDER_SCALE, y * RENDER_SCALE, RENDER_SCALE, RENDER_SCALE);
					break;
				}

				default: {
					const colors = biomeColors[t];
					if (colors) {
						ctx.fillStyle = (x + y) % 3 === 0 ? colors[0] : colors[1];
						ctx.fillRect(x * RENDER_SCALE, y * RENDER_SCALE, RENDER_SCALE, RENDER_SCALE);
					} else if (outerWall && !isInsideWallFast(outerWall, x + 0.5, y + 0.5)) {
						const noise = terrainNoise(x, y, data.seed);
						ctx.fillStyle = noise > 0.52 ? outerTerrain1 : outerTerrain2;
						ctx.fillRect(x * RENDER_SCALE, y * RENDER_SCALE, RENDER_SCALE, RENDER_SCALE);
					}

					break;
				}
			}
		}
	}
}

// ── Field overlays ──────────────────────────────────────────────

function drawFieldOverlays(ctx: Ctx2D, data: MapData): void {
	ctx.lineWidth = 0.5 * RENDER_SCALE;
	for (const field of data.fieldPlots) {
		ctx.save();
		ctx.translate(field.x * RENDER_SCALE, field.y * RENDER_SCALE);
		ctx.rotate(field.rotation);
		ctx.fillStyle = 'rgba(188, 154, 92, 0.15)';
		ctx.fillRect((-field.w * RENDER_SCALE) / 2, (-field.h * RENDER_SCALE) / 2, field.w * RENDER_SCALE, field.h * RENDER_SCALE);
		ctx.strokeStyle = 'rgba(77, 56, 29, 0.25)';
		ctx.strokeRect((-field.w * RENDER_SCALE) / 2, (-field.h * RENDER_SCALE) / 2, field.w * RENDER_SCALE, field.h * RENDER_SCALE);
		ctx.restore();
	}
}

// ── Street / path overlays ──────────────────────────────────────

function drawStreetOverlays(ctx: Ctx2D, data: MapData, urban: boolean): void {
	ctx.lineCap = 'round';
	ctx.lineJoin = 'round';

	// Regular paths
	ctx.lineWidth = Number(RENDER_SCALE);
	ctx.strokeStyle = urban ? 'rgb(170, 170, 170)' : 'rgb(65, 45, 30)';
	ctx.beginPath();
	for (const streetEdge of data.streetEdges) {
		const n1 = data.streetNodes[streetEdge.p1];
		const n2 = data.streetNodes[streetEdge.p2];
		if (!n1.isMain && !n2.isMain) {
			ctx.moveTo(n1.x * RENDER_SCALE, n1.y * RENDER_SCALE);
			ctx.lineTo(n2.x * RENDER_SCALE, n2.y * RENDER_SCALE);
		}
	}

	ctx.stroke();

	// Main roads — wider overlay following actual road centerlines
	ctx.lineWidth = 3 * RENDER_SCALE;
	ctx.strokeStyle = urban ? 'rgba(120, 120, 120, 0.6)' : 'rgb(85, 60, 40)';
	const pathStep = Math.max(1, Math.floor(4 / RENDER_SCALE));
	for (const path of data.mainRoadPaths) {
		ctx.beginPath();
		ctx.moveTo(path[0].x * RENDER_SCALE, path[0].y * RENDER_SCALE);
		for (let pi = pathStep; pi < path.length; pi += pathStep) {
			ctx.lineTo(path[pi].x * RENDER_SCALE, path[pi].y * RENDER_SCALE);
		}

		ctx.lineTo(path.at(-1)!.x * RENDER_SCALE, path.at(-1)!.y * RENDER_SCALE);
		ctx.stroke();
	}
}

// ── Walls & towers (with gate subdivision) ──────────────────────

function drawWalls(ctx: Ctx2D, data: MapData, urban: boolean): void {
	const isVillage = data.mode === 'village';
	for (const wall of data.walls) {
		const segments = getWallSegments(wall);

		// Draw wall segments
		ctx.lineWidth = isVillage ? 1.5 * RENDER_SCALE : 2 * RENDER_SCALE;
		ctx.strokeStyle = isVillage
			? 'rgb(100, 72, 42)'
			: (urban ? 'rgb(70, 70, 70)' : 'rgb(40, 40, 45)');
		ctx.beginPath();
		let hasAny = false;
		for (const seg of segments) {
			if (seg.isGate) {
				continue;
			}

			hasAny = true;
			ctx.moveTo(seg.p1.x * RENDER_SCALE, seg.p1.y * RENDER_SCALE);
			ctx.lineTo(seg.p2.x * RENDER_SCALE, seg.p2.y * RENDER_SCALE);
		}

		if (hasAny) {
			ctx.stroke();
		}

		// Draw towers at non-gate wall vertices
		const towerRadius = (isVillage ? 2 : (urban ? 3 : 4)) * RENDER_SCALE;
		for (const p of wall.nodes) {
			const angle = Math.atan2(p.y - wall.centerY, p.x - wall.centerX);
			if (isGateAngle(wall, angle)) {
				continue;
			}

			ctx.fillStyle = isVillage
				? 'rgb(110, 82, 50)'
				: (urban ? 'rgb(90, 90, 90)' : 'rgb(50, 50, 55)');
			ctx.beginPath();
			ctx.arc(p.x * RENDER_SCALE, p.y * RENDER_SCALE, towerRadius, 0, Math.PI * 2);
			ctx.fill();
			if (urban && !isVillage) {
				ctx.fillStyle = 'rgb(110, 110, 110)';
				ctx.beginPath();
				ctx.arc(p.x * RENDER_SCALE, p.y * RENDER_SCALE, towerRadius * 0.7, 0, Math.PI * 2);
				ctx.fill();
			}
		}

		// Draw gate-edge towers flanking each gate opening (same style as wall towers)
		const gateTowers = getGateTowerPoints(segments);
		for (const pt of gateTowers) {
			ctx.fillStyle = isVillage
				? 'rgb(110, 82, 50)'
				: (urban ? 'rgb(90, 90, 90)' : 'rgb(50, 50, 55)');
			ctx.beginPath();
			ctx.arc(pt.x * RENDER_SCALE, pt.y * RENDER_SCALE, towerRadius, 0, Math.PI * 2);
			ctx.fill();
			if (urban && !isVillage) {
				ctx.fillStyle = 'rgb(110, 110, 110)';
				ctx.beginPath();
				ctx.arc(pt.x * RENDER_SCALE, pt.y * RENDER_SCALE, towerRadius * 0.7, 0, Math.PI * 2);
				ctx.fill();
			}
		}
	}
}

// ── Houses / trees ──────────────────────────────────────────────

function drawHouses(ctx: Ctx2D, data: MapData, urban: boolean): void {
	const isVillage = data.mode === 'village';
	for (let i = 0; i < data.houses.length; i++) {
		const h = data.houses[i];
		const cx = (h.x + (h.w / 2)) * RENDER_SCALE;
		const cy = (h.y + (h.h / 2)) * RENDER_SCALE;
		const pw = h.w * RENDER_SCALE;
		const ph = h.h * RENDER_SCALE;
		const isTree = data.grid[(h.y * data.width) + h.x] === TILE_TREE_DECOR;

		ctx.save();
		ctx.translate(cx, cy);
		if (isTree) {
			// Tree — green circle canopy
			ctx.fillStyle = 'rgb(45, 90, 30)';
			ctx.beginPath();
			ctx.arc(0, -3, pw * 0.9, 0, Math.PI * 2);
			ctx.fill();
			ctx.fillStyle = 'rgb(30, 65, 20)';
			ctx.beginPath();
			ctx.arc(0, 0, pw * 0.7, 0, Math.PI * 2);
			ctx.fill();
		} else if (urban) {
			ctx.rotate(h.rotation);
			if (isVillage) {
				// Village houses — wooden
				ctx.fillStyle = 'rgb(100, 72, 45)';
				ctx.fillRect(-pw / 2, -ph / 2, pw, ph);
				ctx.fillStyle = i % 8 < 5 ? 'rgb(155, 130, 75)' : 'rgb(140, 115, 65)';
				ctx.fillRect(-pw / 2, (-ph / 2) - 3, pw, ph);
			} else {
				// City houses — brick/stone
				ctx.fillStyle = 'rgb(77, 55, 38)';
				ctx.fillRect(-pw / 2, -ph / 2, pw, ph);
				ctx.fillStyle = i % 10 < 7 ? 'rgb(143, 77, 54)' : 'rgb(122, 62, 41)';
				ctx.fillRect(-pw / 2, (-ph / 2) - 4, pw, ph);
				ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
				ctx.strokeRect(-pw / 2, (-ph / 2) - 4, pw, ph);
			}
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
	const distance = Math.sqrt(dx * dx + dy * dy);
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

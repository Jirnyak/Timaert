/**
 * Subworld Canvas2D renderer.
 *
 * Completely independent of the main WebGL pipeline.
 * Draws the subworld view: background image (from CityGenerator),
 * entities, labels, and HUD zone hints.
 *
 * Coordinates are in grid space (0–1024 for a 1024×1024 map).
 * The CityGenerator visual canvas is rendered at `config.scale`
 * pixels per grid tile (typically 2×).
 */

import type {CitizenSpriteSheet} from './citizen-sprites';
import type {SubworldConfig, SubworldEntity} from './types';

// Fallback ground checkerboard tile size (grid units, used only when no bgImage)
const TILE = 32;

/**
 * Map velocity to a direction index matching the sprite sheet layout:
 * 0=front, 1=back, 2=left, 3=right.  Defaults to 0 (front) when idle.
 */
function velocityToDirectionIndex(vx: number, vy: number): number {
	if (Math.abs(vx) < 0.1 && Math.abs(vy) < 0.1) {
		return 0; // Front
	}

	// Prefer horizontal direction when diagonal
	if (Math.abs(vx) >= Math.abs(vy)) {
		return vx < 0 ? 2 : 3; // Left : right
	}

	return vy < 0 ? 1 : 0; // Back : front
}

/**
 * Draw a sprite from an animated CitizenSpriteSheet at the correct
 * frame and direction, given entity velocity and animation state.
 */
function drawAnimatedSprite(
	ctx: CanvasRenderingContext2D,
	sheet: CitizenSpriteSheet,
	characterRow: number,
	entity: SubworldEntity,
	sx: number,
	sy: number,
	drawSize: number,
): void {
	const dirIndex = velocityToDirectionIndex(entity.vx, entity.vy);
	const frame = entity.animFrame ?? 0;
	const cellX = dirIndex * sheet.framesPerDirection + frame;
	const cellY = characterRow;
	const size = sheet.spriteSize;
	ctx.drawImage(
		sheet.canvas,
		cellX * size,
		cellY * size,
		size,
		size,
		sx - drawSize / 2,
		sy - drawSize,
		drawSize,
		drawSize,
	);
}

export class SubworldRenderer {
	private readonly ctx: CanvasRenderingContext2D;

	constructor(private readonly canvas: HTMLCanvasElement) {
		const ctx = canvas.getContext('2d');
		if (!ctx) {
			throw new Error('Failed to get 2d context');
		}

		this.ctx = ctx;
	}

	/** Full redraw for one frame. */
	render(
		config: SubworldConfig,
		cameraX: number,
		cameraY: number,
		scaleOverride?: number,
	): void {
		const {ctx, canvas} = this;
		const dpr = window.devicePixelRatio || 1;
		const cw = Math.round(canvas.clientWidth * dpr);
		const ch = Math.round(canvas.clientHeight * dpr);
		if (canvas.width !== cw || canvas.height !== ch) {
			canvas.width = cw;
			canvas.height = ch;
		}

		const w = canvas.width;
		const h = canvas.height;

		// Scale factor: how many screen pixels per grid tile
		const scale = scaleOverride ?? config.scale ?? 1;

		// Camera offset: player centered, in screen pixels
		const ox = w / 2 - cameraX * scale;
		const oy = h / 2 - cameraY * scale;

		// Clear
		ctx.fillStyle = config.bgColor;
		ctx.fillRect(0, 0, w, h);

		// Draw background
		if (config.bgImage) {
			this.drawBgImage(config, ox, oy, scale);
		} else {
			this.drawGround(config, ox, oy, w, h, scale);
		}

		// World border
		ctx.strokeStyle = 'rgba(255,255,255,0.12)';
		ctx.lineWidth = 2;
		ctx.strokeRect(ox, oy, config.width * scale, config.height * scale);

		// Sort entities by Y for pseudo depth
		const sorted = [...config.entities].sort((a, b) => a.y - b.y);

		for (const entity of sorted) {
			this.drawEntity(config, entity, ox, oy, scale);
		}
	}

	// ── Internals ─────────────────────────────────────────────

	private drawBgImage(config: SubworldConfig, ox: number, oy: number, scale: number): void {
		const {ctx} = this;
		// The CityGenerator visual canvas is rendered at its own internal scale (2px/tile).
		// Stretch it to match the current rendering scale so entities align with the map.
		const worldW = config.width * scale;
		const worldH = config.height * scale;
		ctx.drawImage(config.bgImage!, ox, oy, worldW, worldH);
	}

	private drawGround(
		config: SubworldConfig,
		ox: number, oy: number,
		viewW: number, viewH: number,
		scale: number,
	): void {
		const {ctx} = this;
		const tileScreen = TILE * scale;
		const startCol = Math.max(0, Math.floor(-ox / tileScreen));
		const startRow = Math.max(0, Math.floor(-oy / tileScreen));
		const endCol = Math.min(Math.ceil(config.width / TILE), Math.ceil((viewW - ox) / tileScreen));
		const endRow = Math.min(Math.ceil(config.height / TILE), Math.ceil((viewH - oy) / tileScreen));

		for (let row = startRow; row < endRow; row++) {
			for (let col = startCol; col < endCol; col++) {
				ctx.fillStyle = (row + col) % 2 === 0
					? config.groundColorA
					: config.groundColorB;
				ctx.fillRect(ox + col * tileScreen, oy + row * tileScreen, tileScreen, tileScreen);
			}
		}
	}

	private drawEntity(config: SubworldConfig, entity: SubworldEntity, ox: number, oy: number, scale: number): void {
		const {ctx} = this;
		const sx = ox + entity.x * scale;
		const sy = oy + entity.y * scale;
		const sr = entity.radius * scale;

		switch (entity.kind) {
			case 'building': {
				// Filled rounded rectangle
				const size = sr * 2;
				ctx.fillStyle = entity.color;
				ctx.beginPath();
				this.roundRect(sx - sr, sy - sr, size, size, 4);
				ctx.fill();
				ctx.strokeStyle = 'rgba(0,0,0,0.4)';
				ctx.lineWidth = 1.5;
				ctx.stroke();
				// Roof triangle
				ctx.fillStyle = '#4a3a2a';
				ctx.beginPath();
				ctx.moveTo(sx - sr - 4, sy - sr);
				ctx.lineTo(sx, sy - sr - 14);
				ctx.lineTo(sx + sr + 4, sy - sr);
				ctx.closePath();
				ctx.fill();
				// Label
				this.drawLabel(entity.label, sx, sy + sr + 12);
				break;
			}

			case 'player': {
				const pSheet = config.playerSheet;
				if (pSheet) {
					const drawSize = scale * 1.5;
					drawAnimatedSprite(ctx, pSheet, 0, entity, sx, sy, drawSize);
				} else {
					// Fallback colored circle
					ctx.fillStyle = entity.color;
					ctx.beginPath();
					ctx.arc(sx, sy, sr, 0, Math.PI * 2);
					ctx.fill();
					ctx.strokeStyle = '#fff';
					ctx.lineWidth = 2;
					ctx.stroke();
				}

				break;
			}

			case 'npc': {
				const sheet = config.citizenSheet;
				if (entity.spriteIndex !== undefined && sheet) {
					const drawSize = scale * 1.5;
					drawAnimatedSprite(ctx, sheet, entity.spriteIndex, entity, sx, sy, drawSize);
				} else {
					ctx.fillStyle = entity.color;
					ctx.beginPath();
					ctx.arc(sx, sy, sr, 0, Math.PI * 2);
					ctx.fill();
					ctx.strokeStyle = 'rgba(0,0,0,0.3)';
					ctx.lineWidth = 1;
					ctx.stroke();
				}

				this.drawLabel(entity.label, sx, sy - sr - 4);
				break;
			}

			case 'prop': {
				ctx.fillStyle = entity.color;
				ctx.beginPath();
				ctx.arc(sx, sy, sr, 0, Math.PI * 2);
				ctx.fill();
				break;
			}

			case 'zone': {
				ctx.fillStyle = entity.color;
				ctx.beginPath();
				ctx.arc(sx, sy, sr, 0, Math.PI * 2);
				ctx.fill();
				if (entity.label) {
					this.drawLabel(entity.label, sx, sy + sr + 10);
				}

				break;
			}
		}
	}

	private drawLabel(text: string, x: number, y: number): void {
		if (!text) {
			return;
		}

		const {ctx} = this;
		ctx.font = '11px sans-serif';
		ctx.textAlign = 'center';
		ctx.textBaseline = 'middle';
		// Shadow
		ctx.fillStyle = 'rgba(0,0,0,0.6)';
		ctx.fillText(text, x + 1, y + 1);
		// Foreground
		ctx.fillStyle = '#f0e8d8';
		ctx.fillText(text, x, y);
	}

	private roundRect(x: number, y: number, w: number, h: number, r: number): void {
		const {ctx} = this;
		ctx.moveTo(x + r, y);
		ctx.lineTo(x + w - r, y);
		ctx.quadraticCurveTo(x + w, y, x + w, y + r);
		ctx.lineTo(x + w, y + h - r);
		ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
		ctx.lineTo(x + r, y + h);
		ctx.quadraticCurveTo(x, y + h, x, y + h - r);
		ctx.lineTo(x, y + r);
		ctx.quadraticCurveTo(x, y, x + r, y);
	}
}

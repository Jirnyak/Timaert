// === Spire generator — magical tower landmark ===
//
// A central tall stone tower surrounded by a noisy band of scorched
// land (rock + dirt-like patches). The 3×3 macroworld height field
// is preserved; we only stamp tile-level decoration and add the
// tower as a structure.

import {
	TILE_ROCK, TILE_SQUARE,
	type MapData, type NeighborGrid,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

/** Visible tower properties. */
const TOWER_DIAMETER = 14;
const TOWER_HEIGHT = 96;
const SCORCH_RADIUS = 90;
const CRATER_INNER = 18;
const CRATER_RING = 22;

export class SpireGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'spire', 1);
	}

	generateTiles(_parameter: number, neighbors?: NeighborGrid): void {
		if (neighbors) {
			this.setNeighbors(neighbors);
		}

		this.layGrassBase();
		this.stampScorchedLand();
		this.stampCraterRing();
		this.placeTower();
		this.scatterUniversalTrees(SCORCH_RADIUS);
	}

	/** Random noisy scorched patches around the tower (rock tiles). */
	private stampScorchedLand(): void {
		const cx = this.centerX;
		const cy = this.centerY;
		const r2 = SCORCH_RADIUS * SCORCH_RADIUS;
		for (let y = cy - SCORCH_RADIUS; y <= cy + SCORCH_RADIUS; y++) {
			if (y < 0 || y >= this.height) {
				continue;
			}

			for (let x = cx - SCORCH_RADIUS; x <= cx + SCORCH_RADIUS; x++) {
				if (x < 0 || x >= this.width) {
					continue;
				}

				const dx = x - cx;
				const dy = y - cy;
				const d2 = dx * dx + dy * dy;
				if (d2 > r2) {
					continue;
				}

				const dist = Math.sqrt(d2);
				// Density falls off with distance from tower
				const falloff = 1 - dist / SCORCH_RADIUS;
				const noise = this.smoothTerrainNoise(x * 0.08, y * 0.08);
				if (noise < falloff * 0.55) {
					this.grid[y * this.width + x] = TILE_ROCK;
				}
			}
		}
	}

	/** Crater-like rocky ring around the central tower (visual only). */
	private stampCraterRing(): void {
		const cx = this.centerX;
		const cy = this.centerY;
		const inner = CRATER_INNER;
		const outer = CRATER_INNER + CRATER_RING;
		const inner2 = inner * inner;
		const outer2 = outer * outer;
		for (let y = cy - outer; y <= cy + outer; y++) {
			if (y < 0 || y >= this.height) {
				continue;
			}

			for (let x = cx - outer; x <= cx + outer; x++) {
				if (x < 0 || x >= this.width) {
					continue;
				}

				const dx = x - cx;
				const dy = y - cy;
				const d2 = dx * dx + dy * dy;
				if (d2 < inner2 || d2 > outer2) {
					continue;
				}

				const noise = this.smoothTerrainNoise(x * 0.18 + 200, y * 0.18 + 200);
				this.grid[y * this.width + x] = noise < 0.55 ? TILE_ROCK : TILE_SQUARE;
			}
		}
	}

	/** Tall stone tower at the center. */
	private placeTower(): void {
		this.structures.push(this.makeStructure({
			tag: 'spire_tower',
			shape: 'circle',
			x: this.centerX,
			y: this.centerY,
			w: TOWER_DIAMETER,
			l: TOWER_DIAMETER,
			height: TOWER_HEIGHT,
			rotation: 0,
			roofTexture: 'tower_top',
			wallTexture: 'wall_stone',
			solid: true,
			sprite: false,
		}));
	}
}

/** Functional entry point — used by the generator registry. */
export function generateSpire(
	seed: number, width: number, height: number,
	parameter = 0, neighbors?: NeighborGrid,
): MapData {
	const gen = new SpireGenerator(seed, width, height);
	gen.generateTiles(parameter, neighbors);
	return gen.toMapData();
}

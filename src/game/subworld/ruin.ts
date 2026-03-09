// === Ruin generator — abandoned structure subworld ===
//
// Produces a tile map with crumbling walls, overgrown vegetation,
// and broken roads. Difficulty level controls how many wall rings
// and how dense the rubble/traps are.

import {
	TILE_ROAD, TILE_GRASS, TILE_TREE_DECOR, TILE_SQUARE,
	type MapData,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

export class RuinGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'ruin', 1);
	}

	generateTiles(difficulty: number): void {
		this.layOvergrowth();
		this.carveRuinedPaths();
		this.buildRuinedWalls(difficulty);
		this.placeRuinedSquare();
		this.scatterRubble();
	}

	/** Base the map on overgrown grass. */
	private layOvergrowth(): void {
		for (let i = 0; i < this.grid.length; i++) {
			this.grid[i] = this.rng.random() < 0.55 ? TILE_GRASS : 0;
		}
	}

	/** Carve broken, meandering paths that used to be streets. */
	private carveRuinedPaths(): void {
		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		const pathCount = this.rng.randInt(2, 4);
		for (let i = 0; i < pathCount; i++) {
			const angle = (i * (Math.PI * 2) / pathCount)
				+ this.rng.randFloat(-0.6, 0.6);
			const tx = this.centerX + (Math.cos(angle) * this.width * 0.5);
			const ty = this.centerY + (Math.sin(angle) * this.height * 0.5);
			this.markOrganicMainRoad(this.centerX, this.centerY, tx, ty, angle);
		}

		// Add broken side paths
		const branches = this.rng.randInt(4, 10);
		for (let i = 0; i < branches; i++) {
			this.growStreetBranch();
		}
	}

	/** Build walls with intentional gaps (ruined). */
	private buildRuinedWalls(difficulty: number): void {
		const rings = Math.max(1, Math.min(3, Math.ceil(difficulty / 3)));
		for (let ring = 0; ring < rings; ring++) {
			const radius = this.width * (0.12 + ring * 0.1);
			const segments = this.rng.randInt(10, 16);
			this.buildWall(radius, segments, 0.25);
		}
	}

	/** Place a cracked stone square at center. */
	private placeRuinedSquare(): void {
		const size = this.rng.randInt(5, 9);
		const x = this.centerX - Math.floor(size / 2);
		const y = this.centerY - Math.floor(size / 2);
		for (let dy = 0; dy < size; dy++) {
			for (let dx = 0; dx < size; dx++) {
				const px = x + dx;
				const py = y + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height // Leave random holes in the square
					&& this.rng.random() < 0.8) {
					this.grid[py * this.width + px] = TILE_SQUARE;
				}
			}
		}
	}

	/** Scatter trees and debris among the ruins. */
	private scatterRubble(): void {
		for (let y = 3; y < this.height - 3; y += 3) {
			for (let x = 3; x < this.width - 3; x += 3) {
				const idx = y * this.width + x;
				if (this.grid[idx] === TILE_ROAD || this.grid[idx] === TILE_SQUARE) {
					continue;
				}

				if (this.rng.random() < 0.15) {
					const w = this.rng.randInt(1, 3);
					const h = this.rng.randInt(1, 3);
					this.houses.push({
						x, y, w, h, rotation: this.rng.randFloat(-0.4, 0.4),
					});
					this.grid[idx] = TILE_TREE_DECOR;
				}
			}
		}
	}
}

/** Functional entry point — used by the generator registry. */
export function generateRuin(seed: number, width: number, height: number, difficulty = 1): MapData {
	const gen = new RuinGenerator(seed, width, height);
	gen.generateTiles(difficulty);
	return gen.toMapData();
}

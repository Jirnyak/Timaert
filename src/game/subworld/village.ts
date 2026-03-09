// === Village generator — small rural settlement subworld ===
//
// A lighter variant of the city generator: fewer walls (0–1 ring),
// smaller central square, more farmland, less dense housing.
// Reuses BaseMapGenerator's wall/branching infrastructure.

import {
	TILE_ROAD, TILE_HOUSE, TILE_GRASS, TILE_FIELD, TILE_SQUARE,
	type StreetNode, type MapData,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

export class VillageGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'village', 1);
	}

	generateTiles(population: number): void {
		this.initializeRoads();
		this.generateVillageSquare();
		this.growVillage(population);
		if (population > 300) {
			this.buildWall(this.width / 4, 12, 0.18);
		}

		this.generateFields(population);
	}

	/** Two crossing main roads through center. */
	private initializeRoads(): void {
		const center: StreetNode = {x: this.centerX, y: this.centerY, isMain: true};
		this.streetNodes.push(center);

		const directions = [
			{angle: this.rng.randFloat(-0.3, 0.3), tx: this.width - 1, ty: this.centerY},
			{angle: Math.PI + this.rng.randFloat(-0.3, 0.3), tx: 0, ty: this.centerY},
		];

		for (const dir of directions) {
			this.markOrganicMainRoad(this.centerX, this.centerY, dir.tx, dir.ty, dir.angle);
		}
	}

	/** Small village square at center. */
	private generateVillageSquare(): void {
		const size = this.rng.randInt(4, 7);
		const x = this.centerX - Math.floor(size / 2);
		const y = this.centerY - Math.floor(size / 2);
		for (let dy = 0; dy < size; dy++) {
			for (let dx = 0; dx < size; dx++) {
				const px = x + dx;
				const py = y + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
					this.grid[py * this.width + px] = TILE_SQUARE;
				}
			}
		}
	}

	/** Grow streets and houses around the center. */
	private growVillage(population: number): void {
		const targetHouses = Math.max(5, Math.floor(population / 6));
		let placed = 0;
		let iter = 0;
		const maxIter = targetHouses * 40;

		while (placed < targetHouses && iter < maxIter) {
			iter++;
			const edge = this.growStreetBranch();
			let fails = 0;
			while (fails < 60 && placed < targetHouses) {
				if (this.tryPlaceHouse(edge)) {
					placed++;
					fails = 0;
				} else {
					fails++;
				}
			}
		}
	}

	/** Surround the village with fields and grassland. */
	private generateFields(population: number): void {
		const fieldCount = Math.max(3, Math.floor(population / 30));
		let placed = 0;
		let attempts = 0;

		while (placed < fieldCount && attempts < fieldCount * 20) {
			attempts++;
			const angle = this.rng.randFloat(0, Math.PI * 2);
			const radius = this.rng.randFloat(this.width * 0.15, this.width * 0.4);
			const cx = this.centerX + Math.cos(angle) * radius;
			const cy = this.centerY + Math.sin(angle) * radius;
			const fw = this.rng.randFloat(6, 16);
			const fh = this.rng.randFloat(4, 12);
			const rotation = this.rng.randFloat(-0.5, 0.5);

			const ok = this.placeField(cx, cy, fw, fh, rotation);
			if (ok) {
				placed++;
			}
		}

		// Fill remaining empty outer tiles with grass
		for (let y = 0; y < this.height; y++) {
			for (let x = 0; x < this.width; x++) {
				const idx = y * this.width + x;
				if (this.grid[idx] !== 0) {
					continue;
				}

				const dist = Math.hypot(x - this.centerX, y - this.centerY);
				if (dist > this.width * 0.18 && this.rng.random() < 0.6) {
					this.grid[idx] = TILE_GRASS;
				}
			}
		}
	}

	private placeField(cx: number, cy: number, fw: number, fh: number, rotation: number): boolean {
		const halfDiag = Math.ceil(Math.hypot(fw, fh) / 2) + 2;
		const minX = Math.floor(cx - halfDiag);
		const maxX = Math.ceil(cx + halfDiag);
		const minY = Math.floor(cy - halfDiag);
		const maxY = Math.ceil(cy + halfDiag);

		if (minX < 2 || maxX >= this.width - 2
			|| minY < 2 || maxY >= this.height - 2) {
			return false;
		}

		const cos = Math.cos(rotation);
		const sin = Math.sin(rotation);

		// Verify all cells are empty first
		for (let y = minY; y <= maxY; y++) {
			for (let x = minX; x <= maxX; x++) {
				const lx = (x - cx) * cos + (y - cy) * sin;
				const ly = -(x - cx) * sin + (y - cy) * cos;
				if (Math.abs(lx) <= fw / 2 && Math.abs(ly) <= fh / 2) {
					const idx = y * this.width + x;
					if (this.grid[idx] === TILE_HOUSE || this.grid[idx] === TILE_ROAD) {
						return false;
					}
				}
			}
		}

		// Place
		for (let y = minY; y <= maxY; y++) {
			for (let x = minX; x <= maxX; x++) {
				const lx = (x - cx) * cos + (y - cy) * sin;
				const ly = -(x - cx) * sin + (y - cy) * cos;
				if (Math.abs(lx) <= fw / 2 && Math.abs(ly) <= fh / 2) {
					this.grid[y * this.width + x] = TILE_FIELD;
				}
			}
		}

		this.fieldPlots.push({
			x: cx, y: cy, w: fw, h: fh, rotation,
		});
		return true;
	}
}

/** Functional entry point — used by the generator registry. */
export function generateVillage(seed: number, width: number, height: number, population = 200): MapData {
	const gen = new VillageGenerator(seed, width, height);
	gen.generateTiles(population);
	return gen.toMapData();
}

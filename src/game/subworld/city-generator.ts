// === City generator — settlement subworld ===

import {
	TILE_EMPTY, TILE_ROAD, TILE_HOUSE, TILE_WALL,
	TILE_FIELD, TILE_GRASS, TILE_SQUARE,
	type StreetNode,
} from './map-data';
import {BaseMapGenerator} from './base-generator';

export class CityMapGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024, streetWidth = 1) {
		super(seed, width, height, 'city', streetWidth);
	}

	generateTiles(population: number): void {
		this.initializeMainRoadsThroughGates();
		this.generateCentralSquare(population);
		this.grow(population);
		this.ensureWallsForPopulation(population);
		this.generateOuterLandUse(population);
	}

	// ── Main roads ──────────────────────────────────────────────

	private initializeMainRoadsThroughGates(): void {
		const center: StreetNode = {x: this.centerX, y: this.centerY, isMain: true};
		this.streetNodes.push(center);

		const directions = [
			{angle: 0, targetX: this.width - 1, targetY: this.centerY},
			{angle: Math.PI, targetX: 0, targetY: this.centerY},
			{angle: Math.PI / 2, targetX: this.centerX, targetY: this.height - 1},
			{angle: -(Math.PI / 2), targetX: this.centerX, targetY: 0},
		];

		for (const dir of directions) {
			const nodeId = this.streetNodes.length;
			this.streetNodes.push({x: dir.targetX, y: dir.targetY, isMain: true});
			this.streetEdges.push({p1: 0, p2: nodeId});
			this.markOrganicMainRoad(this.centerX, this.centerY, dir.targetX, dir.targetY, dir.angle);
		}
	}

	// ── Central square ──────────────────────────────────────────

	private generateCentralSquare(population: number): void {
		const baseSize = Math.max(6, Math.min(12, Math.floor(6 + (population / 4000))));
		const size = baseSize * this.streetWidth;
		const x = this.centerX - Math.floor(size / 2);
		const y = this.centerY - Math.floor(size / 2);
		for (let dy = 0; dy < size; dy++) {
			for (let dx = 0; dx < size; dx++) {
				const px = x + dx;
				const py = y + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
					this.grid[(py * this.width) + px] = TILE_SQUARE;
				}
			}
		}
	}

	// ── Growth ──────────────────────────────────────────────────

	private grow(targetPop: number): void {
		const targetStreets = Math.min(800, Math.floor(Math.sqrt(targetPop)));
		const targetHouses = Math.floor(targetPop ** 0.8);
		let housesAdded = this.houses.length;
		const maxIter = Math.max(targetHouses, targetStreets) * 50;
		let iter = 0;

		while (housesAdded < targetHouses && iter < maxIter) {
			iter++;
			const newEdge = this.growStreetBranch();
			let fails = 0;
			const maxFails = 120;
			while (fails < maxFails && housesAdded < targetHouses) {
				if (this.tryPlaceHouse(newEdge)) {
					housesAdded++;
					fails = 0;
				} else {
					fails++;
				}
			}
		}

		this.connectNearbyNodes();
	}

	private connectNearbyNodes(): void {
		const connected = new Set<string>();
		for (const edge of this.streetEdges) {
			const key = Math.min(edge.p1, edge.p2) + ',' + Math.max(edge.p1, edge.p2);
			connected.add(key);
		}

		const maxLinks = Math.min(40, Math.floor(this.streetNodes.length * 0.15));
		let added = 0;
		for (let i = 5; i < this.streetNodes.length && added < maxLinks; i++) {
			const ni = this.streetNodes[i];
			const _dix = ni.x - this.centerX;
			const _diy = ni.y - this.centerY;
			const distI = Math.sqrt(_dix * _dix + _diy * _diy);
			for (let j = i + 1; j < this.streetNodes.length && added < maxLinks; j++) {
				const nj = this.streetNodes[j];
				const _ndx = ni.x - nj.x;
				const _ndy = ni.y - nj.y;
				const nodeDist = Math.sqrt(_ndx * _ndx + _ndy * _ndy);
				if (nodeDist > 25 || nodeDist < 8) {
					continue;
				}

				const _djx = nj.x - this.centerX;
				const _djy = nj.y - this.centerY;
				const distJ = Math.sqrt(_djx * _djx + _djy * _djy);
				const key = i + ',' + j;
				if (Math.abs(distI - distJ) > 15 || this.rng.random() > 0.25 || connected.has(key)) {
					continue;
				}

				this.streetEdges.push({p1: i, p2: j});
				this.markStreetAndRemoveHouses(ni.x, ni.y, nj.x, nj.y);
				connected.add(key);
				added++;
			}
		}
	}

	// ── Walls ──────────────────────────────────────────────────

	private ensureWallsForPopulation(population: number): void {
		const thresholds = [0, 2000, 5000, 10_000, 20_000];
		let targetRings = 1;
		for (let i = 1; i < thresholds.length; i++) {
			if (population >= thresholds[i]) {
				targetRings++;
			}
		}

		const minDimension = Math.min(this.width, this.height);
		while (this.walls.length < targetRings) {
			const ringIndex = this.walls.length;
			const radius = Math.max(18, minDimension * (0.1 + (ringIndex * 0.09)));
			const segments = Math.max(16, 18 + (ringIndex * 6));
			const roughness = 0.12 + (ringIndex * 0.025);
			this.buildWall(radius, segments, roughness);
		}
	}

	// ── Outer land use (fields, grass, roads, farmhouses) ───────

	private generateOuterLandUse(population: number): void {
		const outerWall = this.walls.at(-1);
		if (!outerWall) {
			return;
		}

		// Grass coverage
		const grassThreshold = -0.35 - (population / 50_000);
		for (let y = 2; y < this.height - 2; y++) {
			for (let x = 2; x < this.width - 2; x++) {
				const index = (y * this.width) + x;
				if (this.grid[index] !== TILE_EMPTY) {
					continue;
				}

				if (this.isInsideWall(outerWall, x + 0.5, y + 0.5) || this.hasUrbanNeighbor(x, y, 2)) {
					continue;
				}

				const noise = Math.sin((x + this.rng.worldSeed) * 0.07)
					+ Math.cos((y - this.rng.worldSeed) * 0.09)
					+ Math.sin((x + y) * 0.03);
				if (noise > grassThreshold) {
					this.grid[index] = TILE_GRASS;
				}
			}
		}

		// Farmland
		const targetFields = Math.max(6, Math.min(500, Math.floor(population / 50)));
		let placed = 0;
		let attempts = 0;
		const maxAttempts = targetFields * 25;
		while (placed < targetFields && attempts < maxAttempts) {
			attempts++;
			if (this.tryPlaceFieldPlot(outerWall)) {
				placed++;
			}
		}

		this.generateFarmRoads(outerWall, population);
	}

	private hasUrbanNeighbor(x: number, y: number, radius: number): boolean {
		for (let dy = -radius; dy <= radius; dy++) {
			for (let dx = -radius; dx <= radius; dx++) {
				const px = x + dx;
				const py = y + dy;
				if (px < 0 || px >= this.width || py < 0 || py >= this.height) {
					continue;
				}

				const tile = this.grid[(py * this.width) + px];
				if (tile === TILE_ROAD || tile === TILE_HOUSE || tile === TILE_SQUARE || tile === TILE_WALL) {
					return true;
				}
			}
		}

		return false;
	}

	private tryPlaceFieldPlot(outerWall: {avgRadius: number; centerX: number; centerY: number}): boolean {
		const outerLimit = Math.min(this.width, this.height) * 0.48;
		const angle = this.rng.randFloat(0, Math.PI * 2);
		const radius = this.rng.randFloat(outerWall.avgRadius * 1.05, outerLimit);
		const cx = this.centerX + (Math.cos(angle) * radius);
		const cy = this.centerY + (Math.sin(angle) * radius);
		const fw = this.rng.randFloat(8, 22);
		const fh = this.rng.randFloat(6, 18);
		const rotation = this.rng.randFloat(-0.7, 0.7);

		const halfDiag = Math.ceil(Math.sqrt(fw * fw + fh * fh) / 2) + 2;
		const minX = Math.floor(cx - halfDiag);
		const maxX = Math.ceil(cx + halfDiag);
		const minY = Math.floor(cy - halfDiag);
		const maxY = Math.ceil(cy + halfDiag);

		if (minX < 2 || maxX >= this.width - 2 || minY < 2 || maxY >= this.height - 2) {
			return false;
		}

		const cos = Math.cos(rotation);
		const sin = Math.sin(rotation);

		// Check all cells first
		for (let y = minY; y <= maxY; y++) {
			for (let x = minX; x <= maxX; x++) {
				const localX = ((x - cx) * cos) + ((y - cy) * sin);
				const localY = (-(x - cx) * sin) + ((y - cy) * cos);
				if (Math.abs(localX) > fw / 2 || Math.abs(localY) > fh / 2) {
					continue;
				}

				const outerW = this.walls.at(-1)!;
				if (this.isInsideWall(outerW, x + 0.5, y + 0.5)) {
					return false;
				}

				const tile = this.grid[(y * this.width) + x];
				if (tile !== TILE_EMPTY && tile !== TILE_GRASS) {
					return false;
				}
			}
		}

		// Place
		for (let y = minY; y <= maxY; y++) {
			for (let x = minX; x <= maxX; x++) {
				const localX = ((x - cx) * cos) + ((y - cy) * sin);
				const localY = (-(x - cx) * sin) + ((y - cy) * cos);
				if (Math.abs(localX) > fw / 2 || Math.abs(localY) > fh / 2) {
					continue;
				}

				this.grid[(y * this.width) + x] = TILE_FIELD;
			}
		}

		this.fieldPlots.push({
			x: cx, y: cy, w: fw, h: fh, rotation,
		});
		return true;
	}

	private generateFarmRoads(outerWall: {avgRadius: number; centerX: number; centerY: number}, population: number): void {
		if (this.fieldPlots.length === 0) {
			return;
		}

		const roadCount = Math.min(
			this.fieldPlots.length,
			Math.max(2, Math.floor(population / 1500)),
		);

		const sorted = [...this.fieldPlots].sort((a, b) => {
			const aa = Math.atan2(a.y - outerWall.centerY, a.x - outerWall.centerX);
			const ba = Math.atan2(b.y - outerWall.centerY, b.x - outerWall.centerX);
			return aa - ba;
		});

		const step = Math.max(1, Math.floor(sorted.length / roadCount));
		for (let i = 0; i < sorted.length; i += step) {
			if (i / step >= roadCount) {
				break;
			}

			const field = sorted[i];
			const junction = this.findNearestMainRoadPoint(field.x, field.y);
			this.markLineOnGrid(junction.x, junction.y, field.x, field.y, TILE_ROAD, this.streetWidth);
		}

		this.placeOutskirtHouses(outerWall, population);
	}

	private findNearestMainRoadPoint(fx: number, fy: number): {x: number; y: number} {
		let best = {x: this.centerX, y: this.centerY};
		let bestDist = Number.POSITIVE_INFINITY;
		const sampleStep = 8;
		for (const path of this.mainRoadPaths) {
			for (let i = 0; i < path.length; i += sampleStep) {
				const _pdx = path[i].x - fx;
				const _pdy = path[i].y - fy;
				const d = Math.sqrt(_pdx * _pdx + _pdy * _pdy);
				if (d < bestDist) {
					bestDist = d;
					best = path[i];
				}
			}
		}

		return best;
	}

	private placeOutskirtHouses(outerWall: {avgRadius: number; centerX: number; centerY: number}, population: number): void {
		const targetHuts = Math.max(2, Math.min(60, Math.floor(population / 600)));
		let placed = 0;
		let attempts = 0;
		while (placed < targetHuts && attempts < targetHuts * 30) {
			attempts++;
			const angle = this.rng.randFloat(0, Math.PI * 2);
			const radius = this.rng.randFloat(outerWall.avgRadius * 1.05, outerWall.avgRadius * 1.6);
			const hx = Math.floor(outerWall.centerX + (Math.cos(angle) * radius));
			const hy = Math.floor(outerWall.centerY + (Math.sin(angle) * radius));
			const w = this.rng.randInt(2, 3);
			const h = this.rng.randInt(2, 3);

			if (hx < 5 || hx + w >= this.width - 5 || hy < 5 || hy + h >= this.height - 5) {
				continue;
			}

			if (!this.hasFieldNeighbor(hx, hy, 8)) {
				continue;
			}

			let free = true;
			for (let y = hy; y < hy + h && free; y++) {
				for (let x = hx; x < hx + w && free; x++) {
					const tile = this.grid[(y * this.width) + x];
					if (tile !== TILE_EMPTY && tile !== TILE_GRASS) {
						free = false;
					}
				}
			}

			if (!free) {
				continue;
			}

			for (let y = hy; y < hy + h; y++) {
				for (let x = hx; x < hx + w; x++) {
					this.grid[(y * this.width) + x] = TILE_HOUSE;
				}
			}

			this.houses.push({
				x: hx, y: hy, w, h, rotation: this.rng.randFloat(-0.3, 0.3),
			});
			placed++;
		}
	}

	private hasFieldNeighbor(x: number, y: number, radius: number): boolean {
		for (let dy = -radius; dy <= radius; dy += 2) {
			for (let dx = -radius; dx <= radius; dx += 2) {
				const px = x + dx;
				const py = y + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height
					&& this.grid[(py * this.width) + px] === TILE_FIELD) {
					return true;
				}
			}
		}

		return false;
	}
}

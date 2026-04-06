// === City generator — settlement subworld ===

import {
	TILE_EMPTY, TILE_ROAD, TILE_HOUSE, TILE_WALL,
	TILE_FIELD, TILE_GRASS, TILE_SQUARE, TILE_TREE_DECOR,
	type Point, type StreetNode, type StreetEdge, type WallRing,
	type NeighborGrid, type Dir, DIR_OFFSETS,
	roadDirections, landmarkDirections,
} from './map-data';
import {segmentIntersection, BaseMapGenerator} from './base-generator';

export class CityMapGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024, streetWidth = 1) {
		super(seed, width, height, 'city', streetWidth);
	}

	// Value noise — bilinear interpolation of aperiodic integer hash.
	// Gives smooth coherent shapes (like sin) without diagonal periodicity.
	private smoothNoise(x: number, y: number): number {
		const ix = Math.floor(x);
		const iy = Math.floor(y);
		const fx = x - ix;
		const fy = y - iy;
		// Smoothstep for less blocky interpolation
		const sx = fx * fx * (3 - 2 * fx);
		const sy = fy * fy * (3 - 2 * fy);
		const n00 = this.terrainNoise(ix, iy);
		const n10 = this.terrainNoise(ix + 1, iy);
		const n01 = this.terrainNoise(ix, iy + 1);
		const n11 = this.terrainNoise(ix + 1, iy + 1);
		return n00 * (1 - sx) * (1 - sy)
			+ n10 * sx * (1 - sy)
			+ n01 * (1 - sx) * sy
			+ n11 * sx * sy;
	}

	generateTiles(population: number, neighbors?: NeighborGrid): void {
		if (neighbors) {
			this.neighborGrid = neighbors;
		}

		this.initializeMainRoadsThroughGates(neighbors);
		this.generateCentralSquare(population);
		this.grow(population);
		this.convertExcessRoads();
		this.ensureWallsForPopulation(population);
		this.fillUrbanSpaces();
		this.generateOuterLandUse(population);
		this.generateTreeGradient(this.walls.at(-1), 0.03, 0.4);
	}

	// ── Main roads ──────────────────────────────────────────────

	/**
	 * Create main roads radiating from center to edges.
	 * When a NeighborGrid is provided, roads are directed toward
	 * neighbours that have road features or landmarks — producing
	 * the exact gate count the macroworld connectivity requires.
	 * Falls back to the classic 4-cardinal layout otherwise.
	 */
	private initializeMainRoadsThroughGates(neighbors?: NeighborGrid): void {
		const center: StreetNode = {x: this.centerX, y: this.centerY, isMain: true};
		this.streetNodes.push(center);

		const directions = neighbors
			? this.directionsFromNeighbors(neighbors)
			: [
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

	/**
	 * Derive road directions from neighbour road/landmark features.
	 * Guarantees at least 2 roads (the two widest-apart cardinal dirs).
	 */
	private directionsFromNeighbors(grid: NeighborGrid): Array<{angle: number; targetX: number; targetY: number}> {
		const connDirs = [...new Set([...roadDirections(grid), ...landmarkDirections(grid)])];

		// Always ensure at least 2 roads for playability
		if (connDirs.length < 2) {
			return [
				{angle: 0, targetX: this.width - 1, targetY: this.centerY},
				{angle: Math.PI, targetX: 0, targetY: this.centerY},
				{angle: Math.PI / 2, targetX: this.centerX, targetY: this.height - 1},
				{angle: -(Math.PI / 2), targetX: this.centerX, targetY: 0},
			];
		}

		return connDirs.map(d => this.dirToTarget(d));
	}

	/** Convert a Dir to a target point on the map edge + angle. */
	private dirToTarget(d: Dir): {angle: number; targetX: number; targetY: number} {
		const [dx, dy] = DIR_OFFSETS[d];
		const angle = Math.atan2(dy, dx);
		const targetX = Math.max(1, Math.min(
			this.width - 2,
			Math.round(this.centerX + dx * this.width * 0.49),
		));
		const targetY = Math.max(1, Math.min(
			this.height - 2,
			Math.round(this.centerY + dy * this.height * 0.49),
		));
		return {angle, targetX, targetY};
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

		const maxLinks = Math.min(25, Math.floor(this.streetNodes.length * 0.1));
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

				const edge = {p1: i, p2: j};
				this.streetEdges.push(edge);
				const removed = this.markStreetAndRemoveHouses(ni.x, ni.y, nj.x, nj.y);
				for (let k = 0; k < removed + 3; k++) {
					this.tryPlaceHouse(edge);
				}

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

				const noise = this.smoothNoise(x * 0.07, y * 0.09)
					+ this.smoothNoise(x * 0.03 + y * 0.03, y * 0.03 - x * 0.03)
					+ this.smoothNoise(x * 0.011 + y * 0.011, x * 0.011 - y * 0.011) - 1.5;
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

	// ── Urban fill & outskirt trees ──────────────────────────────

	/** Convert dense road blobs (junctions with no houses) into paved squares. */
	private convertExcessRoads(): void {
		for (let y = 2; y < this.height - 2; y++) {
			for (let x = 2; x < this.width - 2; x++) {
				const idx = (y * this.width) + x;
				if (this.grid[idx] !== TILE_ROAD) {
					continue;
				}

				let roadN = 0;
				let houseN = 0;
				for (let dy = -1; dy <= 1; dy++) {
					for (let dx = -1; dx <= 1; dx++) {
						if (dx === 0 && dy === 0) {
							continue;
						}

						const t = this.grid[((y + dy) * this.width) + x + dx];
						if (t === TILE_ROAD) {
							roadN++;
						}

						if (t === TILE_HOUSE) {
							houseN++;
						}
					}
				}

				if (roadN >= 5 && houseN === 0) {
					this.grid[idx] = TILE_SQUARE;
				}
			}
		}
	}

	/** Fill empty tiles inside city walls with squares, park trees, and grass. */
	private fillUrbanSpaces(): void {
		const outerWall = this.walls.at(-1);
		if (!outerWall) {
			return;
		}

		for (let y = 2; y < this.height - 2; y++) {
			for (let x = 2; x < this.width - 2; x++) {
				const idx = (y * this.width) + x;
				if (this.grid[idx] !== TILE_EMPTY) {
					continue;
				}

				if (!this.isInsideWall(outerWall, x + 0.5, y + 0.5)) {
					continue;
				}

				if (this.hasUrbanNeighbor(x, y, 1)) {
					this.grid[idx] = TILE_SQUARE;
				} else {
					// Open space inside walls — park trees or grass
					const noise = this.smoothNoise(x * 0.03, y * 0.03);
					if (noise > 0.42 && this.rng.random() < 0.35
						&& !this.hasNearbyTile(x, y, TILE_ROAD, 2)) {
						this.grid[idx] = TILE_TREE_DECOR;
						this.houses.push({
							x, y, w: 2, h: 2,
							rotation: this.rng.randFloat(-0.3, 0.3),
						});
					} else {
						this.grid[idx] = TILE_GRASS;
					}
				}
			}
		}
	}

	// ── Street branching + house placement ──────────────────────

	private growStreetBranch(): StreetEdge | undefined {
		if (this.streetNodes.length < 2) {
			return undefined;
		}

		const available = this.streetNodes.length > 5
			? Array.from({length: this.streetNodes.length - 5}, (_, i) => i + 5)
			: [0];
		const parentId = this.selectWeightedParent(available);
		const parent = this.streetNodes[parentId];
		const radialAngle = Math.atan2(parent.y - this.centerY, parent.x - this.centerX);

		for (let i = 0; i < 12; i++) {
			let angle: number;
			if (this.rng.random() < 0.55) {
				const perpBase = radialAngle + (this.rng.random() > 0.5 ? Math.PI / 2 : -(Math.PI / 2));
				angle = perpBase + this.rng.randFloat(-0.4, 0.4);
			} else {
				angle = radialAngle + this.rng.randFloat(-0.6, 0.6);
			}

			const dist = this.rng.randFloat(10, 20) * this.streetWidth;
			const nx = parent.x + (Math.cos(angle) * dist);
			const ny = parent.y + (Math.sin(angle) * dist);

			const margin = 15;
			if (nx < margin || nx >= this.width - margin || ny < margin || ny >= this.height - margin) {
				continue;
			}

			let tooClose = false;
			for (const node of this.streetNodes) {
				const snDx = node.x - nx;
				const snDy = node.y - ny;
				if (Math.sqrt(snDx * snDx + snDy * snDy) < 8 * this.streetWidth) {
					tooClose = true;
					break;
				}
			}

			if (tooClose) {
				continue;
			}

			const newNode: StreetNode = {x: nx, y: ny, isMain: false};
			const newId = this.streetNodes.length;
			this.streetNodes.push(newNode);
			const edge: StreetEdge = {p1: parentId, p2: newId};
			this.streetEdges.push(edge);

			const removed = this.markStreetAndRemoveHouses(parent.x, parent.y, nx, ny);
			for (let k = 0; k < removed; k++) {
				this.tryPlaceHouse();
			}

			for (let k = 0; k < 10; k++) {
				this.tryPlaceHouse(edge);
			}

			return edge;
		}

		return undefined;
	}

	private selectWeightedParent(available: number[]): number {
		if (available.length <= 1) {
			return available[0];
		}

		const maxDist = Math.min(this.width, this.height) * 0.4;
		let totalWeight = 0;
		const weights: number[] = [];
		for (const id of available) {
			const node = this.streetNodes[id];
			const wdx = node.x - this.centerX;
			const wdy = node.y - this.centerY;
			const d = Math.sqrt(wdx * wdx + wdy * wdy);
			const w = Math.max(0.1, 1 - ((d / maxDist) * 0.7));
			weights.push(w);
			totalWeight += w;
		}

		let r = this.rng.random() * totalWeight;
		for (const [i, weight] of weights.entries()) {
			r -= weight;
			if (r <= 0) {
				return available[i];
			}
		}

		return available.at(-1)!;
	}

	private tryPlaceHouse(edge?: StreetEdge): boolean {
		if (this.streetEdges.length === 0) {
			return false;
		}

		let selectedEdge = edge;
		if (!selectedEdge) {
			const regular = this.streetEdges.filter(ed => !this.streetNodes[ed.p1].isMain || !this.streetNodes[ed.p2].isMain);
			if (regular.length === 0) {
				return false;
			}

			selectedEdge = regular[this.rng.randInt(0, regular.length - 1)];
		}

		const n1 = this.streetNodes[selectedEdge.p1];
		const n2 = this.streetNodes[selectedEdge.p2];

		for (let attempt = 0; attempt < 60; attempt++) {
			const t = this.rng.randFloat(0, 1);
			const sx = n1.x + ((n2.x - n1.x) * t);
			const sy = n1.y + ((n2.y - n1.y) * t);
			const side = this.rng.random() > 0.5 ? 1 : -1;
			const dx = n2.x - n1.x;
			const dy = n2.y - n1.y;
			const length = Math.sqrt(dx * dx + dy * dy);
			if (length < 0.1) {
				continue;
			}

			const perpX = -dy / length;
			const perpY = dx / length;
			const distance = this.streetWidth + this.rng.randInt(1, 3);
			const hx = Math.floor(sx + (perpX * distance * side));
			const hy = Math.floor(sy + (perpY * distance * side));
			const w = this.rng.randInt(2, 4);
			const h = this.rng.randInt(2, 4);

			if (hx < 5 || hx >= this.width - 5 || hy < 5 || hy >= this.height - 5) {
				continue;
			}

			let free = true;
			for (let y = hy; y < hy + h && free; y++) {
				for (let x = hx; x < hx + w; x++) {
					if (this.grid[(y * this.width) + x] !== 0) {
						free = false;
						break;
					}
				}
			}

			if (free) {
				for (let y = hy; y < hy + h; y++) {
					for (let x = hx; x < hx + w; x++) {
						this.grid[(y * this.width) + x] = TILE_HOUSE;
					}
				}

				this.houses.push({
					x: hx, y: hy, w, h,
					rotation: Math.atan2(dy, dx) + this.rng.randFloat(-0.3, 0.3),
				});
				return true;
			}
		}

		return false;
	}

	// ── Wall construction ───────────────────────────────────────

	private getCenterOfMass(): Point {
		if (this.streetNodes.length === 0) {
			return {x: this.width / 2, y: this.height / 2};
		}

		let sumX = 0;
		let sumY = 0;
		for (const n of this.streetNodes) {
			sumX += n.x;
			sumY += n.y;
		}

		return {x: sumX / this.streetNodes.length, y: sumY / this.streetNodes.length};
	}

	private buildWall(radius: number, segments: number, roughness = 0.15): void {
		const center = this.getCenterOfMass();
		const angleStep = (Math.PI * 2) / segments;
		const nodes: Point[] = [];
		const phase1 = this.rng.randFloat(0, Math.PI * 2);
		const phase2 = this.rng.randFloat(0, Math.PI * 2);

		for (let i = 0; i < segments; i++) {
			const angle = i * angleStep;
			const harmonic = (Math.sin((angle * 3) + phase1) * 0.32)
				+ (Math.sin((angle * 5) + phase2) * 0.18);
			const radialJitter = this.rng.randFloat(-1, 1) * radius * roughness * 0.18;
			const r = radius + (radius * roughness * harmonic) + radialJitter;
			nodes.push({
				x: center.x + (Math.cos(angle) * r),
				y: center.y + (Math.sin(angle) * r),
			});
		}

		for (let pass = 0; pass < 2; pass++) {
			for (let i = 0; i < nodes.length; i++) {
				const previous = nodes[(i - 1 + nodes.length) % nodes.length];
				const curr = nodes[i];
				const next = nodes[(i + 1) % nodes.length];
				curr.x = (previous.x + (curr.x * 2) + next.x) / 4;
				curr.y = (previous.y + (curr.y * 2) + next.y) / 4;
			}
		}

		let totalRadius = 0;
		for (const p of nodes) {
			const rx = p.x - center.x;
			const ry = p.y - center.y;
			totalRadius += Math.sqrt(rx * rx + ry * ry);
		}

		const avgRadius = totalRadius / nodes.length;
		const gateAngles = this.findRoadCrossingsOnWall(nodes, center.x, center.y);
		const gateHalfArc = Math.max(0.05, (5 + (3 * this.streetWidth)) / avgRadius);

		this.walls.push({
			nodes, avgRadius, centerX: center.x, centerY: center.y, gateAngles, gateHalfArc,
		});
	}

	private findRoadCrossingsOnWall(wallNodes: Point[], cx: number, cy: number): number[] {
		const gateAngles: number[] = [];
		for (const path of this.mainRoadPaths) {
			for (let p = 0; p < path.length - 1; p++) {
				for (let w = 0; w < wallNodes.length; w++) {
					const b1 = wallNodes[w];
					const b2 = wallNodes[(w + 1) % wallNodes.length];
					const crossing = segmentIntersection(path[p], path[p + 1], b1, b2);
					if (crossing) {
						gateAngles.push(Math.atan2(crossing.y - cy, crossing.x - cx));
					}
				}
			}
		}

		return gateAngles.length > 0
			? gateAngles
			: [0, Math.PI, Math.PI / 2, -(Math.PI / 2)];
	}

	private isInsideWall(wall: WallRing, x: number, y: number): boolean {
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

	// ── Tree gradient ───────────────────────────────────────────

	private generateTreeGradient(
		wall: WallRing | undefined,
		minDensity: number,
		maxDensity: number,
	): void {
		const cx = wall ? wall.centerX : this.centerX;
		const cy = wall ? wall.centerY : this.centerY;
		const innerR = wall ? wall.avgRadius * 1.05 : this.width * 0.15;
		const outerR = Math.min(this.width, this.height) * 0.48;
		const rangeR = outerR - innerR;
		if (rangeR <= 0) {
			return;
		}

		for (let y = 2; y < this.height - 2; y += 2) {
			for (let x = 2; x < this.width - 2; x += 2) {
				const idx = (y * this.width) + x;
				const tile = this.grid[idx];
				if (tile !== TILE_EMPTY && tile !== TILE_GRASS) {
					continue;
				}

				if (wall && this.isInsideWall(wall, x + 0.5, y + 0.5)) {
					continue;
				}

				const tdx = x - cx;
				const tdy = y - cy;
				const dist = Math.sqrt((tdx * tdx) + (tdy * tdy));
				if (dist < innerR) {
					continue;
				}

				const t = Math.min(1, (dist - innerR) / rangeR);
				const base = 1 - Math.exp(-4 * t * t);

				const n1 = this.smoothTerrainNoise(x * 0.015, y * 0.015);
				const n2 = this.smoothTerrainNoise(x * 0.04, y * 0.04);
				const n3 = this.smoothTerrainNoise(x * 0.1, y * 0.1);
				const fbm = (n1 * 0.6) + (n2 * 0.3) + (n3 * 0.1);

				const ns = Math.max(0, Math.min(1, (fbm - 0.25) / 0.4));
				const noiseGate = ns * ns * (3 - (2 * ns));

				const density = minDensity + ((maxDensity - minDensity) * base * noiseGate);
				if (this.rng.random() < density) {
					if (this.hasNearbyTile(x, y, TILE_ROAD, 2)
						|| this.hasNearbyTile(x, y, TILE_FIELD, 1)) {
						continue;
					}

					this.grid[idx] = TILE_TREE_DECOR;
					this.houses.push({
						x, y, w: 2, h: 2,
						rotation: this.rng.randFloat(-0.3, 0.3),
					});
				}
			}
		}
	}
}

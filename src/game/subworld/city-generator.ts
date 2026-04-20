// === City generator — settlement subworld ===
// Mycelium-like organic growth: branching streets with grape-cluster houses.

import {
	TILE_EMPTY, TILE_ROAD, TILE_HOUSE, TILE_WALL,
	TILE_FIELD, TILE_SQUARE,
	type Point, type StreetNode, type StreetEdge,
	type NeighborGrid, type Dir, DIR_OFFSETS,
	roadDirections, biomeGroundTile,
} from './map-data';
import {segmentIntersection, BaseMapGenerator, isGroundTile} from './base-generator';

export class CityMapGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'city', 1);
	}

	// Value noise — per-cell (not seamless across boundaries).
	private smoothNoise(x: number, y: number): number {
		return this.smoothLocalNoise(x, y);
	}

	generateTiles(population: number, neighbors?: NeighborGrid): void {
		if (neighbors) {
			this.setNeighbors(neighbors);
		}

		this.initializeMainRoadsThroughGates();
		this.generateCentralSquare(population);
		this.placeKeep(population);
		this.growMycelium(population);
		this.convertExcessRoads();
		this.ensureWallsForPopulation(population);
		this.infillHousesInsideWalls(population);
		this.fillUrbanSpaces();
		this.generateOuterLandUse(population);
		const outerWall = this.walls.at(-1);
		const clearR = outerWall ? outerWall.avgRadius * 1.05 : this.width * 0.15;
		this.scatterUniversalTrees(clearR, outerWall);

		// Fill remaining TILE_EMPTY with biome ground
		const groundTile = biomeGroundTile(this.biome);
		for (let i = 0; i < this.grid.length; i++) {
			if (this.grid[i] === TILE_EMPTY) {
				this.grid[i] = groundTile;
			}
		}
	}

	// ── Main roads ──────────────────────────────────────────────

	private initializeMainRoadsThroughGates(): void {
		const center: StreetNode = {x: this.centerX, y: this.centerY, isMain: true};
		this.streetNodes.push(center);

		const directions = this.neighborGrid
			? this.directionsFromNeighbors()
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

	private directionsFromNeighbors(): Array<{angle: number; targetX: number; targetY: number}> {
		const connDirs = roadDirections(this.neighborGrid!);
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

	private dirToTarget(d: Dir): {angle: number; targetX: number; targetY: number} {
		const anchor = this.anchorFor(d);
		const [dx, dy] = DIR_OFFSETS[d];
		const angle = Math.atan2(dy, dx);
		const targetX = anchor
			? anchor.x
			: Math.max(1, Math.min(this.width - 2, Math.round(this.centerX + dx * this.width * 0.49)));
		const targetY = anchor
			? anchor.y
			: Math.max(1, Math.min(this.height - 2, Math.round(this.centerY + dy * this.height * 0.49)));
		return {angle, targetX, targetY};
	}

	// ── Central keep ────────────────────────────────────────────

	private placeKeep(population: number): void {
		// Keep size scales with population: small fort → large castle
		const baseSize = Math.max(6, Math.min(16, Math.floor(4 + population / 1500)));
		const kw = baseSize + this.rng.randInt(0, 2);
		const kh = baseSize + this.rng.randInt(0, 2);
		const kx = this.centerX - Math.floor(kw / 2);
		const ky = this.centerY - Math.floor(kh / 2) - Math.floor(baseSize / 2) - 2;

		for (let dy = 0; dy < kh; dy++) {
			for (let dx = 0; dx < kw; dx++) {
				const px = kx + dx;
				const py = ky + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
					this.grid[py * this.width + px] = TILE_HOUSE;
				}
			}
		}

		this.houses.push({
			x: kx, y: ky, w: kw, h: kh,
			rotation: this.rng.randFloat(-0.05, 0.05),
		});
	}

	// ── Central square ──────────────────────────────────────────

	private generateCentralSquare(population: number): void {
		const size = Math.max(5, Math.min(10, Math.floor(5 + population / 5000)));
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

	// ── Mycelium growth ─────────────────────────────────────────
	// Each "tip" is a growing point that advances, curves, branches,
	// and places houses along it — like a fungal hypha or grape vine.

	private growMycelium(population: number): void {
		// Houses proportional to population (pop^0.8 gives good density)
		const targetHouses = Math.max(20, Math.floor(population ** 0.8));
		const maxRadius = Math.min(this.width, this.height) * 0.38;

		// Seed initial tips from main road nodes near center
		const tips: GrowTip[] = [];

		for (let i = 1; i < this.streetNodes.length; i++) {
			const n = this.streetNodes[i];
			if (!n.isMain) {
				continue;
			}

			const ang = Math.atan2(n.y - this.centerY, n.x - this.centerX);

			// Place initial branch tips along main roads
			const dist = 25 + this.rng.randFloat(0, 15);
			tips.push({
				x: this.centerX + Math.cos(ang) * dist,
				y: this.centerY + Math.sin(ang) * dist,
				angle: ang + (this.rng.random() > 0.5 ? 0.7 : -0.7),
				depth: 0,
				energy: 8 + this.rng.randInt(0, 4),
			}, {
				x: this.centerX + Math.cos(ang) * dist,
				y: this.centerY + Math.sin(ang) * dist,
				angle: ang + (this.rng.random() > 0.5 ? -0.7 : 0.7),
				depth: 0,
				energy: 8 + this.rng.randInt(0, 4),
			});

			// Additional tip further out
			const dist2 = 50 + this.rng.randFloat(0, 30);
			tips.push({
				x: this.centerX + Math.cos(ang) * dist2,
				y: this.centerY + Math.sin(ang) * dist2,
				angle: ang + this.rng.randFloat(-0.5, 0.5),
				depth: 1,
				energy: 6 + this.rng.randInt(0, 3),
			});
		}

		// Also seed some random tips around center for organic fill
		const extraTips = Math.max(6, Math.floor(population / 1000));
		for (let i = 0; i < extraTips; i++) {
			const ang = this.rng.randFloat(0, Math.PI * 2);
			const dist = this.rng.randFloat(15, 45);
			tips.push({
				x: this.centerX + Math.cos(ang) * dist,
				y: this.centerY + Math.sin(ang) * dist,
				angle: ang + this.rng.randFloat(-0.8, 0.8),
				depth: 0,
				energy: 6 + this.rng.randInt(0, 5),
			});
		}

		let housesPlaced = this.houses.length;
		const pendingTips: GrowTip[] = [];
		let safetyCounter = 0;
		const maxIterations = targetHouses * 50;

		while (tips.length > 0 && housesPlaced < targetHouses && safetyCounter < maxIterations) {
			safetyCounter++;

			// Pick a random tip (prefer ones closer to center for denser core)
			const tipIdx = this.pickTip(tips);
			const tip = tips[tipIdx];

			// Advance the tip
			const stepLength = this.rng.randFloat(10, 20);
			// Gentle curvature — the key to organic feel
			tip.angle += this.rng.randFloat(-0.35, 0.35);

			const nx = tip.x + Math.cos(tip.angle) * stepLength;
			const ny = tip.y + Math.sin(tip.angle) * stepLength;

			// Check bounds
			const margin = 20;
			if (nx < margin || nx >= this.width - margin
				|| ny < margin || ny >= this.height - margin) {
				tips.splice(tipIdx, 1);
				continue;
			}

			// Check distance from center (growth radius)
			const cdx = nx - this.centerX;
			const cdy = ny - this.centerY;
			const centerDist = Math.sqrt(cdx * cdx + cdy * cdy);
			if (centerDist > maxRadius) {
				tips.splice(tipIdx, 1);
				continue;
			}

			// Check not too close to existing nodes (min spacing)
			const minSpacing = 8;
			let tooClose = false;
			for (const node of this.streetNodes) {
				if (node.isMain) {
					continue;
				}

				const snDx = node.x - nx;
				const snDy = node.y - ny;
				if (snDx * snDx + snDy * snDy < minSpacing * minSpacing) {
					tooClose = true;
					break;
				}
			}

			if (tooClose) {
				tip.angle += this.rng.randFloat(-0.5, 0.5);
				tip.energy--;
				if (tip.energy <= 0) {
					tips.splice(tipIdx, 1);
				}

				continue;
			}

			// Place street segment from tip to new position
			const parentId = this.findNearestNode(tip.x, tip.y);
			const newNode: StreetNode = {x: nx, y: ny, isMain: false};
			const newId = this.streetNodes.length;
			this.streetNodes.push(newNode);
			const edge: StreetEdge = {p1: parentId, p2: newId};
			this.streetEdges.push(edge);
			this.markStreetAndRemoveHouses(this.streetNodes[parentId].x, this.streetNodes[parentId].y, nx, ny);

			// Place houses along this segment
			const housesForSegment = this.rng.randInt(3, 8);
			for (let h = 0; h < housesForSegment && housesPlaced < targetHouses; h++) {
				if (this.tryPlaceHouse(edge)) {
					housesPlaced++;
				}
			}

			// Also try placing on random existing edges for fill
			for (let h = 0; h < 3 && housesPlaced < targetHouses; h++) {
				if (this.tryPlaceHouse()) {
					housesPlaced++;
				}
			}

			// Update tip position
			tip.x = nx;
			tip.y = ny;
			tip.energy--;

			// Branch: mycelium splits
			if (tip.depth < 4 && this.rng.random() < branchProbability(tip.depth, centerDist, maxRadius)) {
				const branchAngle = tip.angle
					+ (this.rng.random() > 0.5 ? 1 : -1)
					* (0.5 + this.rng.randFloat(0, 0.6));
				pendingTips.push({
					x: nx,
					y: ny,
					angle: branchAngle,
					depth: tip.depth + 1,
					energy: Math.max(2, tip.energy - 1 + this.rng.randInt(-1, 2)),
				});
			}

			// Exhaust tip → replace from pending
			if (tip.energy <= 0) {
				tips.splice(tipIdx, 1);
			}

			// Refill tips from pending queue
			while (tips.length < 8 && pendingTips.length > 0) {
				tips.push(pendingTips.shift()!);
			}
		}

		// Short cross-links between nearby nodes for organic connectivity
		this.connectNearbyNodes();
	}

	/** Pick a tip with bias toward ones closer to center (denser core). */
	private pickTip(tips: GrowTip[]): number {
		if (tips.length <= 1) {
			return 0;
		}

		let totalWeight = 0;
		for (const tip of tips) {
			const dx = tip.x - this.centerX;
			const dy = tip.y - this.centerY;
			const d = Math.sqrt(dx * dx + dy * dy);
			totalWeight += 1 / (1 + d * 0.005);
		}

		let r = this.rng.random() * totalWeight;
		for (const [i, t] of tips.entries()) {
			const dx = t.x - this.centerX;
			const dy = t.y - this.centerY;
			const d = Math.sqrt(dx * dx + dy * dy);
			r -= 1 / (1 + d * 0.005);
			if (r <= 0) {
				return i;
			}
		}

		return tips.length - 1;
	}

	/** Find the nearest street node to a position. */
	private findNearestNode(x: number, y: number): number {
		let bestId = 0;
		let bestDist = Number.POSITIVE_INFINITY;
		for (let i = 0; i < this.streetNodes.length; i++) {
			const n = this.streetNodes[i];
			const dx = n.x - x;
			const dy = n.y - y;
			const d = dx * dx + dy * dy;
			if (d < bestDist) {
				bestDist = d;
				bestId = i;
			}
		}

		return bestId;
	}

	private connectNearbyNodes(): void {
		const connected = new Set<string>();
		for (const edge of this.streetEdges) {
			const key = Math.min(edge.p1, edge.p2) + ',' + Math.max(edge.p1, edge.p2);
			connected.add(key);
		}

		const maxLinks = Math.min(30, Math.floor(this.streetNodes.length * 0.12));
		let added = 0;
		for (let i = 5; i < this.streetNodes.length && added < maxLinks; i++) {
			const ni = this.streetNodes[i];
			if (ni.isMain) {
				continue;
			}

			for (let j = i + 1; j < this.streetNodes.length && added < maxLinks; j++) {
				const nj = this.streetNodes[j];
				if (nj.isMain) {
					continue;
				}

				const ndx = ni.x - nj.x;
				const ndy = ni.y - nj.y;
				const nodeDist = Math.sqrt(ndx * ndx + ndy * ndy);
				if (nodeDist > 30 || nodeDist < 10) {
					continue;
				}

				if (this.rng.random() > 0.2) {
					continue;
				}

				const key = i + ',' + j;
				if (connected.has(key)) {
					continue;
				}

				const edge: StreetEdge = {p1: i, p2: j};
				this.streetEdges.push(edge);
				const removed = this.markStreetAndRemoveHouses(ni.x, ni.y, nj.x, nj.y);
				for (let k = 0; k < removed + 2; k++) {
					this.tryPlaceHouse(edge);
				}

				connected.add(key);
				added++;
			}
		}
	}

	// ── Infill — fill empty pockets inside walls with houses ────

	private infillHousesInsideWalls(_population: number): void {
		const outerWall = this.walls.at(-1);
		if (!outerWall) {
			return;
		}

		// Scan grid for empty tiles inside outer wall near existing streets
		// and place additional houses to fill gaps
		const infillHouses: Array<{x: number; y: number; w: number; h: number}> = [];
		const scanStep = 6;
		for (let sy = 20; sy < this.height - 20; sy += scanStep) {
			for (let sx = 20; sx < this.width - 20; sx += scanStep) {
				if (!this.isInsideWall(outerWall, sx + 0.5, sy + 0.5)) {
					continue;
				}

				// Check if there's a road within ~15 tiles
				if (!this.hasNearbyRoad(sx, sy, 15)) {
					continue;
				}

				const w = this.rng.randInt(3, 5);
				const h = this.rng.randInt(3, 5);
				const hx = sx + this.rng.randInt(-2, 2);
				const hy = sy + this.rng.randInt(-2, 2);

				if (hx < 5 || hx + w >= this.width - 5 || hy < 5 || hy + h >= this.height - 5) {
					continue;
				}

				if (this.isRectFree(hx - 1, hy - 1, w + 2, h + 2)) {
					infillHouses.push({
						x: hx, y: hy, w, h,
					});
				}
			}
		}

		for (const ih of infillHouses) {
			const {x: ihx, y: ihy, w: ihw, h: ihh} = ih;
			if (!this.isRectFree(ihx, ihy, ihw, ihh)) {
				continue;
			}

			for (let y = ihy; y < ihy + ihh; y++) {
				for (let x = ihx; x < ihx + ihw; x++) {
					this.grid[y * this.width + x] = TILE_HOUSE;
				}
			}

			this.houses.push({
				x: ihx, y: ihy, w: ihw, h: ihh,
				rotation: this.rng.randFloat(-0.3, 0.3),
			});
		}
	}

	private hasNearbyRoad(x: number, y: number, radius: number): boolean {
		for (let dy = -radius; dy <= radius; dy += 3) {
			for (let dx = -radius; dx <= radius; dx += 3) {
				const px = x + dx;
				const py = y + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height
					&& this.grid[py * this.width + px] === TILE_ROAD) {
					return true;
				}
			}
		}

		return false;
	}

	private isRectFree(rx: number, ry: number, rw: number, rh: number): boolean {
		for (let y = ry; y < ry + rh; y++) {
			for (let x = rx; x < rx + rw; x++) {
				if (x < 0 || x >= this.width || y < 0 || y >= this.height) {
					return false;
				}

				if (this.grid[y * this.width + x] !== TILE_EMPTY) {
					return false;
				}
			}
		}

		return true;
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

		// Derive wall radius from actual extent of houses (organic fit)
		const houseExtent = this.computeHouseExtent();
		while (this.walls.length < targetRings) {
			const ringIndex = this.walls.length;
			// Each ring encloses progressively more of the built area
			const fraction = (ringIndex + 1) / targetRings;
			const radius = Math.max(25, houseExtent * fraction * 1.1 + ringIndex * 8);
			const segments = Math.max(16, 18 + ringIndex * 6);
			const roughness = 0.12 + ringIndex * 0.025;
			this.buildWall(radius, segments, roughness);
		}
	}

	/** Compute radius enclosing ~90% of houses from center of mass. */
	private computeHouseExtent(): number {
		if (this.houses.length === 0) {
			return 60;
		}

		const distances: number[] = [];
		for (const h of this.houses) {
			const dx = h.x - this.centerX;
			const dy = h.y - this.centerY;
			distances.push(Math.sqrt(dx * dx + dy * dy));
		}

		distances.sort((a, b) => a - b);
		return distances[Math.floor(distances.length * 0.9)] || 60;
	}

	// ── Outer land use (fields, grass, roads, farmhouses) ───────

	private generateOuterLandUse(population: number): void {
		const outerWall = this.walls.at(-1);
		if (!outerWall) {
			return;
		}

		// Grass coverage
		const grassThreshold = -0.35 - population / 50_000;
		for (let y = 2; y < this.height - 2; y++) {
			for (let x = 2; x < this.width - 2; x++) {
				const index = y * this.width + x;
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
					this.grid[index] = biomeGroundTile(this.biome);
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

				const tile = this.grid[py * this.width + px];
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
		const cx = this.centerX + Math.cos(angle) * radius;
		const cy = this.centerY + Math.sin(angle) * radius;
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

		for (let y = minY; y <= maxY; y++) {
			for (let x = minX; x <= maxX; x++) {
				const localX = (x - cx) * cos + (y - cy) * sin;
				const localY = -(x - cx) * sin + (y - cy) * cos;
				if (Math.abs(localX) > fw / 2 || Math.abs(localY) > fh / 2) {
					continue;
				}

				const outerW = this.walls.at(-1)!;
				if (this.isInsideWall(outerW, x + 0.5, y + 0.5)) {
					return false;
				}

				const tile = this.grid[y * this.width + x];
				if (tile !== TILE_EMPTY && !isGroundTile(tile)) {
					return false;
				}
			}
		}

		for (let y = minY; y <= maxY; y++) {
			for (let x = minX; x <= maxX; x++) {
				const localX = (x - cx) * cos + (y - cy) * sin;
				const localY = -(x - cx) * sin + (y - cy) * cos;
				if (Math.abs(localX) > fw / 2 || Math.abs(localY) > fh / 2) {
					continue;
				}

				this.grid[y * this.width + x] = TILE_FIELD;
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
				const pdx = path[i].x - fx;
				const pdy = path[i].y - fy;
				const d = Math.sqrt(pdx * pdx + pdy * pdy);
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
			const hx = Math.floor(outerWall.centerX + Math.cos(angle) * radius);
			const hy = Math.floor(outerWall.centerY + Math.sin(angle) * radius);
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
					const tile = this.grid[y * this.width + x];
					if (tile !== TILE_EMPTY && !isGroundTile(tile)) {
						free = false;
					}
				}
			}

			if (!free) {
				continue;
			}

			for (let y = hy; y < hy + h; y++) {
				for (let x = hx; x < hx + w; x++) {
					this.grid[y * this.width + x] = TILE_HOUSE;
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
					&& this.grid[py * this.width + px] === TILE_FIELD) {
					return true;
				}
			}
		}

		return false;
	}

	private countNeighborTypes(x: number, y: number): {roadN: number; houseN: number} {
		let roadN = 0;
		let houseN = 0;
		for (let dy = -1; dy <= 1; dy++) {
			for (let dx = -1; dx <= 1; dx++) {
				if (dx === 0 && dy === 0) {
					continue;
				}

				const t = this.grid[(y + dy) * this.width + x + dx];
				if (t === TILE_ROAD) {
					roadN++;
				}

				if (t === TILE_HOUSE) {
					houseN++;
				}
			}
		}

		return {roadN, houseN};
	}

	// ── Urban fill ──────────────────────────────────────────────

	private convertExcessRoads(): void {
		for (let y = 2; y < this.height - 2; y++) {
			for (let x = 2; x < this.width - 2; x++) {
				const idx = y * this.width + x;
				if (this.grid[idx] !== TILE_ROAD) {
					continue;
				}

				const {roadN, houseN} = this.countNeighborTypes(x, y);

				if (roadN >= 5 && houseN === 0) {
					this.grid[idx] = TILE_SQUARE;
				}
			}
		}
	}

	private fillUrbanSpaces(): void {
		const outerWall = this.walls.at(-1);
		if (!outerWall) {
			return;
		}

		for (let y = 2; y < this.height - 2; y++) {
			for (let x = 2; x < this.width - 2; x++) {
				const idx = y * this.width + x;
				if (this.grid[idx] !== TILE_EMPTY) {
					continue;
				}

				if (!this.isInsideWall(outerWall, x + 0.5, y + 0.5)) {
					continue;
				}

				this.grid[idx] = this.hasUrbanNeighbor(x, y, 1)
					? TILE_SQUARE
					: biomeGroundTile(this.biome);
			}
		}
	}

	// ── House placement ─────────────────────────────────────────

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
			const t = this.rng.randFloat(0.05, 0.95);
			const sx = n1.x + (n2.x - n1.x) * t;
			const sy = n1.y + (n2.y - n1.y) * t;
			const side = this.rng.random() > 0.5 ? 1 : -1;
			const dx = n2.x - n1.x;
			const dy = n2.y - n1.y;
			const length = Math.sqrt(dx * dx + dy * dy);
			if (length < 0.1) {
				continue;
			}

			const perpX = -dy / length;
			const perpY = dx / length;
			const distance = 2 + this.rng.randInt(1, 4);
			const hx = Math.floor(sx + perpX * distance * side);
			const hy = Math.floor(sy + perpY * distance * side);
			const w = this.rng.randInt(3, 5);
			const h = this.rng.randInt(3, 5);

			if (hx < 5 || hx >= this.width - 5 || hy < 5 || hy >= this.height - 5) {
				continue;
			}

			let free = true;
			for (let y = hy; y < hy + h && free; y++) {
				for (let x = hx; x < hx + w; x++) {
					if (this.grid[y * this.width + x] !== 0) {
						free = false;
						break;
					}
				}
			}

			if (free) {
				for (let y = hy; y < hy + h; y++) {
					for (let x = hx; x < hx + w; x++) {
						this.grid[y * this.width + x] = TILE_HOUSE;
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
			const harmonic = Math.sin(angle * 3 + phase1) * 0.32
				+ Math.sin(angle * 5 + phase2) * 0.18;
			const radialJitter = this.rng.randFloat(-1, 1) * radius * roughness * 0.18;
			const r = radius + radius * roughness * harmonic + radialJitter;
			nodes.push({
				x: center.x + Math.cos(angle) * r,
				y: center.y + Math.sin(angle) * r,
			});
		}

		for (let pass = 0; pass < 2; pass++) {
			for (let i = 0; i < nodes.length; i++) {
				const previous = nodes[(i - 1 + nodes.length) % nodes.length];
				const curr = nodes[i];
				const next = nodes[(i + 1) % nodes.length];
				curr.x = (previous.x + curr.x * 2 + next.x) / 4;
				curr.y = (previous.y + curr.y * 2 + next.y) / 4;
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
		const gateHalfArc = Math.max(0.08, (8 + 4 * this.streetWidth) / avgRadius);

		this.walls.push({
			nodes, avgRadius, centerX: center.x, centerY: center.y, gateAngles, gateHalfArc,
		});
	}

	private findRoadCrossingsOnWall(wallNodes: Point[], cx: number, cy: number): number[] {
		const gateAngles: number[] = [];
		const addedAngles = new Set<number>();

		const addGate = (angle: number) => {
			// Quantize to avoid duplicate gates at nearly the same angle
			const q = Math.round(angle * 50);
			if (!addedAngles.has(q)) {
				addedAngles.add(q);
				gateAngles.push(angle);
			}
		};

		// Check main road paths
		for (const path of this.mainRoadPaths) {
			for (let p = 0; p < path.length - 1; p++) {
				for (let w = 0; w < wallNodes.length; w++) {
					const b1 = wallNodes[w];
					const b2 = wallNodes[(w + 1) % wallNodes.length];
					const crossing = segmentIntersection(path[p], path[p + 1], b1, b2);
					if (crossing) {
						addGate(Math.atan2(crossing.y - cy, crossing.x - cx));
					}
				}
			}
		}

		// Check branch street edges that cross the wall
		for (const edge of this.streetEdges) {
			const n1 = this.streetNodes[edge.p1];
			const n2 = this.streetNodes[edge.p2];
			for (let w = 0; w < wallNodes.length; w++) {
				const b1 = wallNodes[w];
				const b2 = wallNodes[(w + 1) % wallNodes.length];
				const crossing = segmentIntersection(n1, n2, b1, b2);
				if (crossing) {
					addGate(Math.atan2(crossing.y - cy, crossing.x - cx));
				}
			}
		}

		return gateAngles.length > 0
			? gateAngles
			: [0, Math.PI, Math.PI / 2, -(Math.PI / 2)];
	}
}

// ── Growth tip data ─────────────────────────────────────────────

type GrowTip = {
	x: number;
	y: number;
	angle: number;
	depth: number;
	energy: number;
};

/** Branch probability — higher near center, decreasing outward. */
function branchProbability(depth: number, centerDist: number, maxRadius: number): number {
	const distFactor = 1 - centerDist / maxRadius;
	const depthPenalty = depth * 0.12;
	return Math.max(0.05, 0.45 * distFactor - depthPenalty);
}

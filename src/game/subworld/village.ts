// === Village generator — rural settlement subworld ===
//
// Wooden palisade (частокол), rectangular log houses along roads,
// farmland outside the wall, and forest gradient toward map edges.

import {
	TILE_EMPTY, TILE_ROAD, TILE_HOUSE, TILE_GRASS, TILE_FIELD,
	TILE_SQUARE, TILE_TREE_DECOR,
	type Point, type StreetNode, type MapData, type WallRing,
	type NeighborGrid, DIR_OFFSETS,
	roadDirections, landmarkDirections,
} from './map-data';
import {segmentIntersection, BaseMapGenerator} from './base-generator';

export class VillageGenerator extends BaseMapGenerator {
	constructor(seed: number, width = 1024, height = 1024) {
		super(seed, width, height, 'village', 1);
	}

	generateTiles(population: number, neighbors?: NeighborGrid): void {
		if (neighbors) {
			this.neighborGrid = neighbors;
		}

		this.initializeRoads(neighbors);
		this.generateVillageSquare();
		this.growVillage(population);
		this.generateFields(population);
		if (population >= 50) {
			this.buildPalisade(population);
		}

		this.fillInnerGaps();
		this.generateTreeGradient(this.walls.at(-1), 0.05, 0.55);
	}

	// ── Roads ───────────────────────────────────────────────────

	/**
	 * Main roads through center.
	 * When a NeighborGrid is provided, roads align to neighbours
	 * with road features or landmarks. Otherwise uses the legacy
	 * east-west + optional cross-road layout.
	 */
	private initializeRoads(neighbors?: NeighborGrid): void {
		const center: StreetNode = {x: this.centerX, y: this.centerY, isMain: true};
		this.streetNodes.push(center);

		const dirs: Array<{angle: number; tx: number; ty: number}> = neighbors
			? this.directionsFromNeighbors(neighbors)
			: this.legacyDirections();

		for (const dir of dirs) {
			const nodeId = this.streetNodes.length;
			this.streetNodes.push({x: dir.tx, y: dir.ty, isMain: true});
			this.streetEdges.push({p1: 0, p2: nodeId});
			this.markOrganicMainRoad(this.centerX, this.centerY, dir.tx, dir.ty, dir.angle);
		}
	}

	private legacyDirections(): Array<{angle: number; tx: number; ty: number}> {
		const dirs: Array<{angle: number; tx: number; ty: number}> = [
			{
				angle: this.rng.randFloat(-0.15, 0.15),
				tx: this.width - 1, ty: this.centerY,
			},
			{
				angle: Math.PI + this.rng.randFloat(-0.15, 0.15),
				tx: 0, ty: this.centerY,
			},
		];

		if (this.rng.random() < 0.65) {
			dirs.push(
				{
					angle: (Math.PI / 2) + this.rng.randFloat(-0.15, 0.15),
					tx: this.centerX, ty: this.height - 1,
				},
				{
					angle: -(Math.PI / 2) + this.rng.randFloat(-0.15, 0.15),
					tx: this.centerX, ty: 0,
				},
			);
		}

		return dirs;
	}

	private directionsFromNeighbors(grid: NeighborGrid): Array<{angle: number; tx: number; ty: number}> {
		const connDirs = [...new Set([...roadDirections(grid), ...landmarkDirections(grid)])];
		if (connDirs.length < 2) {
			return this.legacyDirections();
		}

		return connDirs.map(d => {
			const [dx, dy] = DIR_OFFSETS[d];
			const angle = Math.atan2(dy, dx);
			return {
				angle,
				tx: Math.max(1, Math.min(this.width - 2, Math.round(this.centerX + dx * this.width * 0.49))),
				ty: Math.max(1, Math.min(this.height - 2, Math.round(this.centerY + dy * this.height * 0.49))),
			};
		});
	}

	// ── Central square ──────────────────────────────────────────

	/** Small village square at center. */
	private generateVillageSquare(): void {
		const size = this.rng.randInt(3, 6);
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

	/**
	 * Settle radius — houses are placed within this distance from
	 * center. Palisade and fields radiate outward from this core.
	 * Scales with population: small hamlet ≈ 30 tiles, big village ≈ 70.
	 */
	private settleRadius(population: number): number {
		return Math.min(this.width * 0.07, 30 + Math.sqrt(population) * 3);
	}

	/** Place houses along main roads, constrained to the settle radius. */
	private growVillage(population: number): void {
		const targetHouses = Math.max(1, Math.floor(population / 5));
		const maxR = this.settleRadius(population);
		let placed = 0;
		let iter = 0;
		const maxIter = targetHouses * 50;

		while (placed < targetHouses && iter < maxIter) {
			iter++;
			const edge = this.streetEdges[this.rng.randInt(0, this.streetEdges.length - 1)];
			if (!edge) {
				continue;
			}

			if (this.tryPlaceVillageHouse(edge, maxR)) {
				placed++;
			}
		}
	}

	/**
	 * Village-specific house placement: only within `maxRadius` of center.
	 * Houses are small log cabins (2–3 tiles) along main road edges.
	 */
	private tryPlaceVillageHouse(edge: {p1: number; p2: number}, maxRadius: number): boolean {
		const n1 = this.streetNodes[edge.p1];
		const n2 = this.streetNodes[edge.p2];
		const edgeDx = n2.x - n1.x;
		const edgeDy = n2.y - n1.y;
		const edgeLength = Math.sqrt(edgeDx * edgeDx + edgeDy * edgeDy);
		if (edgeLength < 0.1) {
			return false;
		}

		const perpX = -edgeDy / edgeLength;
		const perpY = edgeDx / edgeLength;

		for (let attempt = 0; attempt < 40; attempt++) {
			const t = this.rng.randFloat(0, 1);
			const sx = n1.x + (edgeDx * t);
			const sy = n1.y + (edgeDy * t);

			// Reject if beyond settle radius
			const distX = sx - this.centerX;
			const distY = sy - this.centerY;
			if (distX * distX + distY * distY > maxRadius * maxRadius) {
				continue;
			}

			const side = this.rng.random() > 0.5 ? 1 : -1;
			const offset = this.streetWidth + this.rng.randInt(1, 2);
			const hx = Math.floor(sx + (perpX * offset * side));
			const hy = Math.floor(sy + (perpY * offset * side));
			const w = this.rng.randInt(2, 3);
			const h = this.rng.randInt(2, 3);

			if (hx < 5 || hx >= this.width - 5 || hy < 5 || hy >= this.height - 5) {
				continue;
			}

			let free = true;
			for (let cy = hy; cy < hy + h && free; cy++) {
				for (let cx = hx; cx < hx + w; cx++) {
					if (this.grid[(cy * this.width) + cx] !== 0) {
						free = false;
						break;
					}
				}
			}

			if (free) {
				for (let cy = hy; cy < hy + h; cy++) {
					for (let cx = hx; cx < hx + w; cx++) {
						this.grid[(cy * this.width) + cx] = TILE_HOUSE;
					}
				}

				this.houses.push({
					x: hx, y: hy, w, h,
					rotation: Math.atan2(edgeDy, edgeDx) + this.rng.randFloat(-0.3, 0.3),
				});
				return true;
			}
		}

		return false;
	}

	// ── Palisade ────────────────────────────────────────────────

	/** Build a wooden palisade (частокол) around the settlement. */
	private buildPalisade(_population: number): void {
		// Radius encloses the settled area: compute from outermost house
		let maxHouseDist = 0;
		for (const h of this.houses) {
			if (this.grid[h.y * this.width + h.x] === TILE_HOUSE) {
				const dx = (h.x + h.w / 2) - this.centerX;
				const dy = (h.y + h.h / 2) - this.centerY;
				const d = Math.sqrt(dx * dx + dy * dy);
				if (d > maxHouseDist) {
					maxHouseDist = d;
				}
			}
		}

		// Tight fit: enclose houses + small margin
		const radius = Math.max(15, maxHouseDist + 6);
		const segments = Math.max(10, Math.floor(12 + (radius / 8)));
		this.buildWall(radius, segments, 0.12);
	}

	// ── Fields ──────────────────────────────────────────────────

	/** Place rectangular farmland plots around the village. */
	private generateFields(population: number): void {
		const wall = this.walls.at(-1);
		const innerR = wall ? wall.avgRadius + 3 : this.settleRadius(population) + 5;
		const outerR = innerR + Math.min(this.width * 0.08, 30 + population * 0.3);

		const fieldCount = Math.max(2, Math.floor(population / 20));
		let placed = 0;
		let attempts = 0;

		while (placed < fieldCount && attempts < fieldCount * 25) {
			attempts++;
			const angle = this.rng.randFloat(0, Math.PI * 2);
			const r = this.rng.randFloat(innerR, outerR);
			const cx = this.centerX + (Math.cos(angle) * r);
			const cy = this.centerY + (Math.sin(angle) * r);
			const fw = this.rng.randFloat(8, 20);
			const fh = this.rng.randFloat(6, 16);
			const rotation = this.rng.randFloat(-0.4, 0.4);

			if (this.placeField(cx, cy, fw, fh, rotation)) {
				placed++;
			}
		}
	}

	// ── Inner fill ──────────────────────────────────────────────

	/** Fill empty tiles: grass/squares inside palisade, grass outside. */
	private fillInnerGaps(): void {
		const outerWall = this.walls.at(-1);
		for (let y = 0; y < this.height; y++) {
			for (let x = 0; x < this.width; x++) {
				const idx = (y * this.width) + x;
				if (this.grid[idx] !== 0) {
					continue;
				}

				const inside = outerWall
					&& this.isInsideWall(outerWall, x + 0.5, y + 0.5);

				if (inside) {
					this.grid[idx] = this.rng.random() < 0.5
						? TILE_GRASS
						: TILE_SQUARE;
				} else {
					const _dx = x - this.centerX;
					const _dy = y - this.centerY;
					const dist = Math.sqrt((_dx * _dx) + (_dy * _dy));
					const threshold = outerWall
						? outerWall.avgRadius * 0.8
						: this.width * 0.12;
					if (dist > threshold) {
						this.grid[idx] = TILE_GRASS;
					}
				}
			}
		}
	}

	// ── Helpers ─────────────────────────────────────────────────

	/** Place a single rotated field rectangle. */
	private placeField(cx: number, cy: number, fw: number, fh: number, rotation: number): boolean {
		const halfDiag = Math.ceil(Math.sqrt((fw * fw) + (fh * fh)) / 2) + 2;
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

		// Verify all cells first
		for (let y = minY; y <= maxY; y++) {
			for (let x = minX; x <= maxX; x++) {
				const lx = ((x - cx) * cos) + ((y - cy) * sin);
				const ly = (-(x - cx) * sin) + ((y - cy) * cos);
				if (Math.abs(lx) <= fw / 2 && Math.abs(ly) <= fh / 2) {
					const tile = this.grid[(y * this.width) + x];
					if (tile === TILE_HOUSE || tile === TILE_ROAD) {
						return false;
					}
				}
			}
		}

		// Place
		for (let y = minY; y <= maxY; y++) {
			for (let x = minX; x <= maxX; x++) {
				const lx = ((x - cx) * cos) + ((y - cy) * sin);
				const ly = (-(x - cx) * sin) + ((y - cy) * cos);
				if (Math.abs(lx) <= fw / 2 && Math.abs(ly) <= fh / 2) {
					this.grid[(y * this.width) + x] = TILE_FIELD;
				}
			}
		}

		this.fieldPlots.push({
			x: cx, y: cy, w: fw, h: fh, rotation,
		});
		return true;
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

/** Functional entry point — used by the generator registry. */
export function generateVillage(seed: number, width: number, height: number, population = 200): MapData {
	const gen = new VillageGenerator(seed, width, height);
	gen.generateTiles(population);
	return gen.toMapData();
}

// === Base map generator — shared grid + wall logic ===

import {
	TILE_EMPTY, TILE_ROAD, TILE_HOUSE, TILE_WALL,
	type Point, type StreetNode, type StreetEdge,
	type House, type FieldPlot, type WallRing, type MapData,
	type SubworldMode, MapRng, angularDistance, isGateAngle,
} from './map-data';

/** Abstract base — subclassed by CityGenerator and NatureGenerator. */
export abstract class BaseMapGenerator {
	readonly width: number;
	readonly height: number;
	readonly mode: SubworldMode;
	readonly streetWidth: number;
	readonly rng: MapRng;
	grid: Uint8Array;

	readonly streetNodes: StreetNode[] = [];
	readonly streetEdges: StreetEdge[] = [];
	readonly houses: House[] = [];
	readonly walls: WallRing[] = [];
	readonly fieldPlots: FieldPlot[] = [];
	readonly mainRoadPaths: Point[][] = [];

	readonly centerX: number;
	readonly centerY: number;

	constructor(seed: number, width: number, height: number, mode: SubworldMode, streetWidth = 1) {
		this.rng = new MapRng(seed);
		this.width = width;
		this.height = height;
		this.mode = mode;
		this.streetWidth = streetWidth;
		this.grid = new Uint8Array(width * height);
		this.centerX = Math.floor(width / 2) + (this.rng.random() > 0.5 ? this.rng.randInt(-10, 10) : 0);
		this.centerY = Math.floor(height / 2) + (this.rng.random() > 0.5 ? this.rng.randInt(-10, 10) : 0);
	}

	/** Generate the tile map. Must be implemented by subclasses. */
	abstract generateTiles(value: number): void;

	/** Produce a MapData snapshot — call after generateTiles. */
	toMapData(): MapData {
		return {
			grid: this.grid,
			width: this.width,
			height: this.height,
			spawnX: this.centerX,
			spawnY: this.centerY,
			seed: this.rng.worldSeed,
			mode: this.mode,
			houses: this.houses,
			walls: this.walls,
			fieldPlots: this.fieldPlots,
			mainRoadPaths: this.mainRoadPaths,
			streetNodes: this.streetNodes,
			streetEdges: this.streetEdges,
		};
	}

	// ── Grid primitives ──────────────────────────────────────────

	markLineOnGrid(x1: number, y1: number, x2: number, y2: number, value: number, width = 1): void {
		const _dx0 = x2 - x1;
		const _dy0 = y2 - y1;
		const dist = Math.sqrt(_dx0 * _dx0 + _dy0 * _dy0);
		const steps = Math.ceil(dist * 2);
		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			let x = x1 + (_dx0 * t);
			let y = y1 + (_dy0 * t);
			if (i > 0 && i < steps) {
				x += this.rng.randFloat(-0.8, 0.8);
				y += this.rng.randFloat(-0.8, 0.8);
			}

			const ix = Math.floor(x);
			const iy = Math.floor(y);
			const hw = Math.floor(width / 2);
			for (let dy = -hw; dy <= hw; dy++) {
				for (let dx = -hw; dx <= hw; dx++) {
					const px = ix + dx;
					const py = iy + dy;
					if (px >= 0 && px < this.width && py >= 0 && py < this.height
						&& this.grid[py * this.width + px] !== TILE_HOUSE) {
						this.grid[py * this.width + px] = value;
					}
				}
			}
		}
	}

	/** Mark an organic main road and store centerline. */
	markOrganicMainRoad(x1: number, y1: number, x2: number, y2: number, baseAngle: number): void {
		const _dx1 = x2 - x1;
		const _dy1 = y2 - y1;
		const dist = Math.sqrt(_dx1 * _dx1 + _dy1 * _dy1);
		const steps = Math.ceil(dist);
		const streetCells = new Set<number>();
		const centerline: Point[] = [];

		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			let x = x1 + ((x2 - x1) * t);
			let y = y1 + ((y2 - y1) * t);
			if (t > 0.08 && t < 0.92) {
				const waveFreq = 0.015;
				const waveAmp = 3.5 + this.rng.randFloat(-1, 1);
				const perpAngle = baseAngle + (Math.PI / 2);
				const offset = Math.sin((t * dist * waveFreq) + this.rng.worldSeed) * waveAmp;
				x += Math.cos(perpAngle) * offset;
				y += Math.sin(perpAngle) * offset;
			}

			centerline.push({x, y});
			const ix = Math.floor(x);
			const iy = Math.floor(y);
			for (let dy = -this.streetWidth; dy <= this.streetWidth; dy++) {
				for (let dx = -this.streetWidth; dx <= this.streetWidth; dx++) {
					const px = ix + dx;
					const py = iy + dy;
					if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
						streetCells.add((py * this.width) + px);
					}
				}
			}
		}

		this.mainRoadPaths.push(centerline);
		for (const idx of streetCells) {
			this.grid[idx] = TILE_ROAD;
		}
	}

	/** Mark a street segment, removing overlapping houses. Returns houses removed. */
	markStreetAndRemoveHouses(x1: number, y1: number, x2: number, y2: number): number {
		const streetCells = new Set<number>();
		const _dx2 = x2 - x1;
		const _dy2 = y2 - y1;
		const dist = Math.sqrt(_dx2 * _dx2 + _dy2 * _dy2);
		const steps = Math.ceil(dist * 2);
		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			const cx = Math.floor(x1 + (_dx2 * t));
			const cy = Math.floor(y1 + (_dy2 * t));
			for (let dy = -this.streetWidth; dy <= this.streetWidth; dy++) {
				for (let dx = -this.streetWidth; dx <= this.streetWidth; dx++) {
					const px = cx + dx;
					const py = cy + dy;
					if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
						streetCells.add((py * this.width) + px);
					}
				}
			}
		}

		const toRemove: number[] = [];
		for (let i = 0; i < this.houses.length; i++) {
			const h = this.houses[i];
			if (this.houseOverlaps(h, streetCells)) {
				toRemove.push(i);
				this.clearHouseFromGrid(h);
			}
		}

		for (let i = toRemove.length - 1; i >= 0; i--) {
			this.houses.splice(toRemove[i], 1);
		}

		for (const idx of streetCells) {
			this.grid[idx] = TILE_ROAD;
		}

		return toRemove.length;
	}

	private houseOverlaps(h: House, cells: Set<number>): boolean {
		for (let hy = h.y; hy < h.y + h.h; hy++) {
			for (let hx = h.x; hx < h.x + h.w; hx++) {
				if (cells.has((hy * this.width) + hx)) {
					return true;
				}
			}
		}

		return false;
	}

	private clearHouseFromGrid(h: House): void {
		for (let hy = h.y; hy < h.y + h.h; hy++) {
			for (let hx = h.x; hx < h.x + h.w; hx++) {
				if (hx >= 0 && hx < this.width && hy >= 0 && hy < this.height
					&& this.grid[(hy * this.width) + hx] === TILE_HOUSE) {
					this.grid[(hy * this.width) + hx] = TILE_EMPTY;
				}
			}
		}
	}

	// ── Branching + houses ──────────────────────────────────────

	growStreetBranch(): StreetEdge | undefined {
		if (this.streetNodes.length < 5) {
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
				const _snDx = node.x - nx;
				const _snDy = node.y - ny;
				if (Math.sqrt(_snDx * _snDx + _snDy * _snDy) < 8 * this.streetWidth) {
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

	selectWeightedParent(available: number[]): number {
		if (available.length <= 1) {
			return available[0];
		}

		const maxDist = Math.min(this.width, this.height) * 0.4;
		let totalWeight = 0;
		const weights: number[] = [];
		for (const id of available) {
			const node = this.streetNodes[id];
			const _wdx = node.x - this.centerX;
			const _wdy = node.y - this.centerY;
			const d = Math.sqrt(_wdx * _wdx + _wdy * _wdy);
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

	tryPlaceHouse(edge?: StreetEdge): boolean {
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
			for (let y = hy; y < hy + h; y++) {
				for (let x = hx; x < hx + w; x++) {
					if (this.grid[(y * this.width) + x] !== 0) {
						free = false;
						break;
					}
				}

				if (!free) {
					break;
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

	// ── Walls ──────────────────────────────────────────────────

	getCenterOfMass(): Point {
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

	buildWall(radius: number, segments: number, roughness = 0.15): void {
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
			const _rx = p.x - center.x;
			const _ry = p.y - center.y;
			totalRadius += Math.sqrt(_rx * _rx + _ry * _ry);
		}

		const avgRadius = totalRadius / nodes.length;
		const gateAngles = this.mode === 'city'
			? this.findRoadCrossingsOnWall(nodes, center.x, center.y)
			: [];
		const gateHalfArc = Math.max(0.05, (5 + (3 * this.streetWidth)) / avgRadius);

		this.walls.push({
			nodes, avgRadius, centerX: center.x, centerY: center.y, gateAngles, gateHalfArc,
		});
	}

	// ── Gate detection ──────────────────────────────────────────

	findRoadCrossingsOnWall(wallNodes: Point[], cx: number, cy: number): number[] {
		const gateAngles: number[] = [];
		for (const path of this.mainRoadPaths) {
			const crossing = this.findCenterlineCrossing(path, wallNodes);
			if (crossing) {
				gateAngles.push(Math.atan2(crossing.y - cy, crossing.x - cx));
			}
		}

		return gateAngles.length > 0
			? gateAngles
			: [0, Math.PI, Math.PI / 2, -(Math.PI / 2)];
	}

	findCenterlineCrossing(path: Point[], wallNodes: Point[]): Point | undefined {
		for (let p = 0; p < path.length - 1; p++) {
			const a1 = path[p];
			const a2 = path[p + 1];
			for (let w = 0; w < wallNodes.length; w++) {
				const b1 = wallNodes[w];
				const b2 = wallNodes[(w + 1) % wallNodes.length];
				const hit = segmentIntersection(a1, a2, b1, b2);
				if (hit) {
					return hit;
				}
			}
		}

		return undefined;
	}

	isInsideWall(wall: WallRing, x: number, y: number): boolean {
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

	terrainNoise(x: number, y: number): number {
		let value = (x * 374_761_393) ^ (y * 668_265_263) ^ (this.rng.worldSeed * 2_246_822_519);
		value = (value ^ (value >>> 13)) * 1_274_126_177;
		value ^= value >>> 16;
		return (value >>> 0) / 4_294_967_295;
	}
}

// ── Free functions ──────────────────────────────────────────────

export function segmentIntersection(a1: Point, a2: Point, b1: Point, b2: Point): Point | undefined {
	const dx1 = a2.x - a1.x;
	const dy1 = a2.y - a1.y;
	const dx2 = b2.x - b1.x;
	const dy2 = b2.y - b1.y;
	const denom = (dx1 * dy2) - (dy1 * dx2);
	if (Math.abs(denom) < 0.001) {
		return undefined;
	}

	const t = (((b1.x - a1.x) * dy2) - ((b1.y - a1.y) * dx2)) / denom;
	const u = (((b1.x - a1.x) * dy1) - ((b1.y - a1.y) * dx1)) / denom;
	if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
		return {x: a1.x + (t * dx1), y: a1.y + (t * dy1)};
	}

	return undefined;
}

/**
 * Generate traversability grid from a MapData.
 * Splits wall segments at gate boundaries for correct passability.
 */
export function generateTraversability(data: MapData): Uint8Array {
	const {grid, width, height, walls} = data;
	const trav = new Uint8Array(width * height);

	for (let i = 0; i < trav.length; i++) {
		const tile = grid[i];
		trav[i] = (tile === TILE_HOUSE || tile === TILE_WALL) ? 0 : 255;
	}

	for (const wall of walls) {
		const segments = getWallSegments(wall);
		for (const seg of segments) {
			if (seg.isGate) {
				continue;
			}

			const _sdx = seg.p2.x - seg.p1.x;
			const _sdy = seg.p2.y - seg.p1.y;
			const dist = Math.sqrt(_sdx * _sdx + _sdy * _sdy);
			const steps = Math.ceil(dist * 2);
			for (let k = 0; k <= steps; k++) {
				const t = k / steps;
				const x = Math.floor(seg.p1.x + ((seg.p2.x - seg.p1.x) * t));
				const y = Math.floor(seg.p1.y + ((seg.p2.y - seg.p1.y) * t));
				blockCell(trav, width, height, x, y, 1);
			}

			// Tower at non-gate start vertex
			const startAngle = Math.atan2(seg.p1.y - wall.centerY, seg.p1.x - wall.centerX);
			if (!isGateAngle(wall, startAngle)) {
				blockTower(trav, width, height, seg.p1, 3);
			}
		}

		// Block gate-edge towers (flanking each gate opening)
		const gateTowers = getGateTowerPoints(segments);
		for (const pt of gateTowers) {
			blockTower(trav, width, height, pt, 3);
		}
	}

	return trav;
}

export function getTraversabilityData(data: MapData): {
	width: number;
	height: number;
	data: Uint8Array;
	heightData: Uint8Array;
	roadData: Uint8Array;
	iceData: Uint8Array;
} {
	const {grid, width, height} = data;
	const length = width * height;
	const trav = generateTraversability(data);
	const heightData = new Uint8Array(length).fill(128);
	const roadData = new Uint8Array(length);
	const iceData = new Uint8Array(length).fill(0);
	for (let i = 0; i < length; i++) {
		roadData[i] = grid[i] === TILE_ROAD ? 255 : 0;
	}

	return {
		width, height, data: trav, heightData, roadData, iceData,
	};
}

function blockCell(data: Uint8Array, width: number, height: number, x: number, y: number, radius = 0): void {
	for (let dy = -radius; dy <= radius; dy++) {
		for (let dx = -radius; dx <= radius; dx++) {
			const px = x + dx;
			const py = y + dy;
			if (px >= 0 && px < width && py >= 0 && py < height) {
				data[(py * width) + px] = 0;
			}
		}
	}
}

function blockTower(data: Uint8Array, width: number, height: number, center: Point, radius: number): void {
	for (let ty = -radius; ty <= radius; ty++) {
		for (let tx = -radius; tx <= radius; tx++) {
			if ((tx * tx) + (ty * ty) <= radius * radius) {
				blockCell(data, width, height, Math.floor(center.x + tx), Math.floor(center.y + ty), 0);
			}
		}
	}
}

// ── Wall sub-segmentation for gates ─────────────────────────────

export type WallSegment = {
	p1: Point;
	p2: Point;
	isGate: boolean;
};

/**
 * Split each polygon edge of a wall into sub-segments, clipping at gate
 * boundaries so that gate openings are geometrically precise regardless
 * of wall vertex density.
 */
export function getWallSegments(wall: WallRing): WallSegment[] {
	const result: WallSegment[] = [];
	const {nodes, centerX, centerY, gateAngles, gateHalfArc} = wall;

	for (let i = 0; i < nodes.length; i++) {
		const p1 = nodes[i];
		const p2 = nodes[(i + 1) % nodes.length];
		splitEdgeByGates(result, p1, p2, centerX, centerY, gateAngles, gateHalfArc);
	}

	return result;
}

function splitEdgeByGates(
	out: WallSegment[],
	p1: Point, p2: Point,
	cx: number, cy: number,
	gateAngles: number[], gateHalfArc: number,
): void {
	if (gateAngles.length === 0) {
		out.push({p1, p2, isGate: false});
		return;
	}

	// Collect t-values where the edge crosses gate boundary angles
	const cuts: Array<{t: number; entering: boolean}> = [];
	const steps = 32; // Sample the edge for angle transitions
	let previousInGate = isAngleInAnyGate(Math.atan2(p1.y - cy, p1.x - cx), gateAngles, gateHalfArc);

	for (let s = 1; s <= steps; s++) {
		const t = s / steps;
		const x = p1.x + ((p2.x - p1.x) * t);
		const y = p1.y + ((p2.y - p1.y) * t);
		const angle = Math.atan2(y - cy, x - cx);
		const inGate = isAngleInAnyGate(angle, gateAngles, gateHalfArc);
		if (inGate !== previousInGate) {
			// Binary search for precise crossing
			const preciseT = binarySearchBoundary(p1, p2, cx, cy, gateAngles, gateHalfArc, (s - 1) / steps, t, previousInGate);
			cuts.push({t: preciseT, entering: inGate});
		}

		previousInGate = inGate;
	}

	if (cuts.length === 0) {
		// Entire edge is either gate or wall
		out.push({p1, p2, isGate: previousInGate});
		return;
	}

	// Sort by t
	cuts.sort((a, b) => a.t - b.t);

	let currentIsGate = isAngleInAnyGate(Math.atan2(p1.y - cy, p1.x - cx), gateAngles, gateHalfArc);
	let lastPoint = p1;
	let lastT = 0;

	for (const cut of cuts) {
		if (cut.t - lastT > 0.001) {
			const midPoint = lerpPoint(p1, p2, cut.t);
			out.push({p1: lastPoint, p2: midPoint, isGate: currentIsGate});
			lastPoint = midPoint;
		}

		lastT = cut.t;
		currentIsGate = cut.entering;
	}

	// Final segment
	if (1 - lastT > 0.001) {
		out.push({p1: lastPoint, p2, isGate: currentIsGate});
	}
}

function binarySearchBoundary(
	p1: Point, p2: Point,
	cx: number, cy: number,
	gateAngles: number[], gateHalfArc: number,
	tLow: number, tHigh: number,
	lowIsGate: boolean,
): number {
	for (let iter = 0; iter < 12; iter++) {
		const tMid = (tLow + tHigh) / 2;
		const x = p1.x + ((p2.x - p1.x) * tMid);
		const y = p1.y + ((p2.y - p1.y) * tMid);
		const angle = Math.atan2(y - cy, x - cx);
		const midIsGate = isAngleInAnyGate(angle, gateAngles, gateHalfArc);
		if (midIsGate === lowIsGate) {
			tLow = tMid;
		} else {
			tHigh = tMid;
		}
	}

	return (tLow + tHigh) / 2;
}

function isAngleInAnyGate(angle: number, gateAngles: number[], gateHalfArc: number): boolean {
	for (const ga of gateAngles) {
		if (angularDistance(ga, angle) <= gateHalfArc) {
			return true;
		}
	}

	return false;
}

function lerpPoint(a: Point, b: Point, t: number): Point {
	return {x: a.x + ((b.x - a.x) * t), y: a.y + ((b.y - a.y) * t)};
}

/** Extract the two boundary points flanking each gate opening. */
export function getGateTowerPoints(segments: WallSegment[]): Point[] {
	const points: Point[] = [];
	for (let i = 0; i < segments.length; i++) {
		const seg = segments[i];
		const previous = segments[(i - 1 + segments.length) % segments.length];
		const next = segments[(i + 1) % segments.length];
		if (seg.isGate) {
			if (!previous.isGate) {
				points.push(seg.p1);
			}

			if (!next.isGate) {
				points.push(seg.p2);
			}
		}
	}

	return points;
}

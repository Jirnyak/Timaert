// === Organic City Generator ===
// Implements mycelium-like street growth, dynamic house placement, and wall generation.
export const TILE_EMPTY = 0;
export const TILE_ROAD = 1;
export const TILE_HOUSE = 2; // В лесу = густое дерево
export const TILE_WALL = 3; // В лесу = скала
export const TILE_SQUARE = 6; // В лесу = поляна
export const TILE_TREE_DECOR = 7; // Одиночное дерево

export type SubworldMode = 'city' | 'nature';

export type CityMapData = {
	visual: HTMLCanvasElement;
	grid: Uint8Array; // 0=blocked, 255=traversable
	width: number;
	height: number;
	spawnX: number;
	spawnY: number;
};

type StreetNode = {
	x: number;
	y: number;
	isMain: boolean;
};

type StreetEdge = {
	p1: number; // Index in nodes
	p2: number; // Index in nodes
};

type House = {
	x: number;
	y: number;
	w: number;
	h: number;
	rotation: number;
};

export class CityGenerator {
	private readonly width: number;
	private readonly height: number;
	private seed: number;
	private readonly mode: SubworldMode;

	// 0=empty, 1=street, 2=house
	private grid: Uint8Array;

	private readonly streetNodes: StreetNode[] = [];
	private readonly streetEdges: StreetEdge[] = [];
	private readonly houses: House[] = [];
	private readonly walls: Array<Array<{x: number; y: number}>> = [];
	private readonly wallsBuilt = new Set<number>();

	private readonly centerX: number;
	private readonly centerY: number;

	constructor(seed: number, width = 1024, height = 1024, mode: SubworldMode = 'city') {
		this.seed = seed;
		this.width = width;
		this.height = height;
		this.mode = mode;
		this.grid = new Uint8Array(width * height);
		// Смещение центра для органичности (из deterministic_city.py)
		this.centerX = Math.floor(width / 2) + (this.random() > 0.5 ? this.randInt(-10, 10) : 0);
		this.centerY = Math.floor(height / 2) + (this.random() > 0.5 ? this.randInt(-10, 10) : 0);
	}

	private random(): number {
		const x = Math.sin(this.seed++) * 10_000;
		return x - Math.floor(x);
	}

	private randInt(min: number, max: number): number {
		return Math.floor(this.random() * (max - min + 1)) + min;
	}

	private randFloat(min: number, max: number): number {
		return this.random() * (max - min) + min;
	}

	public generate(value: number): CityMapData {
		if (this.mode === 'city') {
			// 1. Initialize main roads first (mycelium starts from center)
			this.initializeMainRoadsThroughGates();
			// 2. Generate central square
			this.generateCentralSquare(value);
			// 3. Grow streets and houses (mycelium-like branching)
			this.grow(value);
		} else {
			// Nature mode logic
			this.initializeNaturePaths();
			this.growNature(value);
		}

		const visual = this.render();
		const traversability = this.generateTraversability();

		return {
			visual,
			grid: traversability,
			width: this.width,
			height: this.height,
			spawnX: this.centerX,
			spawnY: this.centerY,
		};
	}

	private initializeMainRoadsThroughGates() {
		const center: StreetNode = {x: this.centerX, y: this.centerY, isMain: true};
		this.streetNodes.push(center);

		// Create main cross roads
		const edgeNodes: StreetNode[] = [
			{x: 0, y: this.centerY, isMain: true},
			{x: this.width - 1, y: this.centerY, isMain: true},
			{x: this.centerX, y: 0, isMain: true},
			{x: this.centerX, y: this.height - 1, isMain: true},
		];

		for (const node of edgeNodes) {
			const nodeId = this.streetNodes.length;
			this.streetNodes.push(node);
			this.streetEdges.push({p1: 0, p2: nodeId});
		}

		// Mark the main roads
		for (let i = 1; i < 5; i++) {
			const n1 = this.streetNodes[0];
			const n2 = this.streetNodes[i];
			this.markStreetAndRemoveHouses(n1.x, n1.y, n2.x, n2.y);
		}
	}

	private generateCentralSquare(population: number) {
		// Логика из city_map_generator.py: размер зависит от населения
		const size = Math.max(6, Math.min(12, Math.floor(6 + population / 4000)));
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

	private initializeNaturePaths() {
		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		// В лесу тропы идут в случайных направлениях, а не крестом
		for (let i = 0; i < 4; i++) {
			const angle = (i * Math.PI / 2) + this.randFloat(-0.5, 0.5);
			const tx = this.centerX + Math.cos(angle) * this.width;
			const ty = this.centerY + Math.sin(angle) * this.width;
			this.markLineOnGrid(this.centerX, this.centerY, tx, ty, TILE_ROAD, 1);
		}
	}

	private growNature(density: number) {
		const targetNodes = Math.floor(density / 10);
		for (let i = 0; i < targetNodes; i++) {
			const edge = this.growStreetBranch();
			if (edge) {
				// В лесу вместо домов спавним "узлы" деревьев
				for (let k = 0; k < 5; k++) {
					this.tryPlaceHouse(edge);
				}
			}
		}

		// Вместо стен в лесу - случайные скалы по краям кора
		if (density > 5000) {
			this.buildWall(this.width / 3, 10);
		}
	}

	private markLineOnGrid(x1: number, y1: number, x2: number, y2: number, value: number, width = 1) {
		const dist = Math.hypot(x2 - x1, y2 - y1);
		const steps = Math.ceil(dist * 2); // Больше шагов для плавности кривых

		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			let x = x1 + (x2 - x1) * t;
			let y = y1 + (y2 - y1) * t;

			// Органические флуктуации (из mark_street_segment_organic в grape_city.py)
			if (i > 0 && i < steps) {
				x += this.randFloat(-0.8, 0.8);
				y += this.randFloat(-0.8, 0.8);
			}

			const ix = Math.floor(x);
			const iy = Math.floor(y);

			const hw = Math.floor(width / 2);
			for (let dy = -hw; dy <= hw; dy++) {
				for (let dx = -hw; dx <= hw; dx++) {
					const px = ix + dx;
					const py = iy + dy;
					if (px >= 0 && px < this.width && py >= 0 && py < this.height // Используем константу TILE_HOUSE
						&& this.grid[py * this.width + px] !== TILE_HOUSE) {
						this.grid[py * this.width + px] = value;
					}
				}
			}
		}
	}

	// Returns number of houses removed
	private markStreetAndRemoveHouses(x1: number, y1: number, x2: number, y2: number): number {
		// Find cells covered by this street segment
		const streetCells = new Set<number>();
		const dist = Math.hypot(x2 - x1, y2 - y1);
		const steps = Math.ceil(dist * 2); // Higher density for check

		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			const cx = Math.floor(x1 + (x2 - x1) * t);
			const cy = Math.floor(y1 + (y2 - y1) * t);

			// Buffer 1 cell
			for (let dy = -1; dy <= 1; dy++) {
				for (let dx = -1; dx <= 1; dx++) {
					const px = cx + dx;
					const py = cy + dy;
					if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
						streetCells.add(py * this.width + px);
					}
				}
			}
		}

		// Find overlapping houses
		const toRemove: number[] = [];
		for (let i = 0; i < this.houses.length; i++) {
			const h = this.houses[i];
			let overlap = false;
			// Simple box check against street path cells
			// A more precise way: check if any cell of the house is in streetCells
			for (let hy = h.y; hy < h.y + h.h; hy++) {
				for (let hx = h.x; hx < h.x + h.w; hx++) {
					if (streetCells.has(hy * this.width + hx)) {
						overlap = true;
						break;
					}
				}

				if (overlap) {
					break;
				}
			}

			if (overlap) {
				toRemove.push(i);
				// Clear grid
				for (let hy = h.y; hy < h.y + h.h; hy++) {
					for (let hx = h.x; hx < h.x + h.w; hx++) {
						if (hx >= 0 && hx < this.width && hy >= 0 && hy < this.height && this.grid[hy * this.width + hx] === TILE_HOUSE) {
							this.grid[hy * this.width + hx] = TILE_EMPTY;
						}
					}
				}
			}
		}

		// Remove from array (backwards)
		for (let i = toRemove.length - 1; i >= 0; i--) {
			this.houses.splice(toRemove[i], 1);
		}

		// Mark street on grid
		for (const idx of streetCells) {
			this.grid[idx] = TILE_ROAD;
		}

		return toRemove.length;
	}

	private growStreetBranch(): {p1: number; p2: number} | undefined {
		if (this.streetNodes.length < 5) {
			return undefined;
		}

		// Match LandmarkSiteOverlay: only grow from center until we have non-main nodes
		const available = this.streetNodes.length > 5
			? Array.from({length: this.streetNodes.length - 5}, (_, index) => index + 5)
			: [0];
		const parentId = available[this.randInt(0, available.length - 1)];
		const parent = this.streetNodes[parentId];

		// Try angles
		for (let i = 0; i < 12; i++) {
			const angle = this.randFloat(0, Math.PI * 2);
			const dist = this.randFloat(10, 20); // Length 10-20

			const nx = parent.x + Math.cos(angle) * dist;
			const ny = parent.y + Math.sin(angle) * dist;

			const margin = 15;
			if (nx < margin || nx >= this.width - margin || ny < margin || ny >= this.height - margin) {
				continue;
			}

			// Proximity check to existing nodes
			let tooClose = false;
			for (const node of this.streetNodes) {
				if (Math.hypot(node.x - nx, node.y - ny) < 8) {
					tooClose = true;
					break;
				}
			}

			if (tooClose) {
				continue;
			}

			// Accept new node
			const newNode = {x: nx, y: ny, isMain: false};
			const newId = this.streetNodes.length;
			this.streetNodes.push(newNode);
			this.streetEdges.push({p1: parentId, p2: newId});

			// Handle houses
			const removed = this.markStreetAndRemoveHouses(parent.x, parent.y, nx, ny);

			// Compensate
			for (let k = 0; k < removed; k++) {
				this.tryPlaceHouse();
			}

			// Densify
			for (let k = 0; k < 10; k++) {
				this.tryPlaceHouse({p1: parentId, p2: newId});
			}

			return {p1: parentId, p2: newId};
		}

		return undefined;
	}

	private tryPlaceHouse(edge?: StreetEdge): boolean {
		if (this.streetEdges.length === 0) {
			return false;
		}

		// Pick edge
		let selectedEdge = edge;
		if (!selectedEdge) {
			// Pick random REGULAR edge (not main)
			const regular = this.streetEdges.filter(ed =>
				!this.streetNodes[ed.p1].isMain || !this.streetNodes[ed.p2].isMain);
			if (regular.length === 0) {
				return false;
			}

			selectedEdge = regular[this.randInt(0, regular.length - 1)];
		}

		const n1 = this.streetNodes[selectedEdge.p1];
		const n2 = this.streetNodes[selectedEdge.p2];

		// Try placement
		for (let attempt = 0; attempt < 60; attempt++) {
			const t = this.randFloat(0, 1);
			const sx = n1.x + (n2.x - n1.x) * t;
			const sy = n1.y + (n2.y - n1.y) * t;

			const side = this.random() > 0.5 ? 1 : -1;
			const dx = n2.x - n1.x;
			const dy = n2.y - n1.y;
			const length = Math.hypot(dx, dy);
			if (length < 0.1) {
				continue;
			}

			// Perpendicular placement (Faithful port from grape_city.py try_place_house)
			const perpX = -dy / length;
			const perpY = dx / length;

			// Дистанция от дороги 2-4 клетки для плотной застройки
			const distance = this.randInt(2, 4);
			const hx = Math.floor(sx + perpX * distance * side);
			const hy = Math.floor(sy + perpY * distance * side);

			const w = this.randInt(2, 4);
			const h = this.randInt(2, 4);

			// Check bounds
			if (hx < 5 || hx >= this.width - 5 || hy < 5 || hy >= this.height - 5) {
				continue;
			}

			// Check occupancy
			let free = true;
			for (let y = hy; y < hy + h; y++) {
				for (let x = hx; x < hx + w; x++) {
					if (this.grid[y * this.width + x] !== 0) {
						free = false;
						break;
					}
				}

				if (!free) {
					break;
				}
			}

			if (free) {
				// Place
				for (let y = hy; y < hy + h; y++) {
					for (let x = hx; x < hx + w; x++) {
						this.grid[y * this.width + x] = TILE_HOUSE;
					}
				}

				const angle = Math.atan2(dy, dx) + (this.randFloat(-0.3, 0.3));
				this.houses.push({
					x: hx, y: hy, w, h, rotation: angle,
				});
				return true;
			}
		}

		return false;
	}

	private getCenterOfMass(): {x: number; y: number} {
		if (this.streetNodes.length === 0) {
			return {x: this.width / 2, y: this.height / 2};
		}

		let sumX = 0;
		let sumY = 0;
		for (const n of this.streetNodes) {
			sumX += n.x;
			sumY += n.y;
		}

		return {
			x: sumX / this.streetNodes.length,
			y: sumY / this.streetNodes.length,
		};
	}

	private buildWall(radius: number, segments: number) {
		const center = this.getCenterOfMass();
		const angleStep = (Math.PI * 2) / segments;
		const nodes: Array<{x: number; y: number}> = [];

		for (let i = 0; i < segments; i++) {
			const angle = i * angleStep + this.randFloat(-0.1, 0.1);
			const r = radius + this.randInt(-2, 2);
			nodes.push({
				x: center.x + Math.cos(angle) * r,
				y: center.y + Math.sin(angle) * r,
			});
		}

		this.walls.push(nodes);
	}

	private grow(targetPop: number) {
		// Initialize main roads if not done
		if (this.streetNodes.length === 0) {
			this.initializeMainRoadsThroughGates();
		}

		const targetStreets = Math.min(800, Math.floor(Math.sqrt(targetPop)));
		const targetHouses = Math.floor(targetPop ** 0.8);

		let housesAdded = this.houses.length;
		const maxIter = Math.max(targetHouses, targetStreets) * 50;
		let iter = 0;

		while (housesAdded < targetHouses && iter < maxIter) {
			iter++;

			// Grow street branch
			const newEdge = this.growStreetBranch();

			// Place houses
			let fails = 0;
			const maxFails = 120;
			while (fails < maxFails && housesAdded < targetHouses) {
				if (this.tryPlaceHouse(newEdge)) {
					housesAdded++;
					fails = 0;

					// Wall check
					const thresholds = [1000, 10_000];
					for (const t of thresholds) {
						if (housesAdded >= t && !this.wallsBuilt.has(t)) {
							const radius = t === 1000 ? Math.min(this.width, this.height) / 8 : Math.min(this.width, this.height) / 4;
							const segs = t === 1000 ? 6 : 8;
							this.buildWall(radius, segs);
							this.wallsBuilt.add(t);
						}
					}
				} else {
					fails++;
				}
			}
		}
	}

	// === Rendering ===

	private render(): HTMLCanvasElement {
		const c = document.createElement('canvas');
		const SCALE = 2; // Reduced from 4 to 2 for better performance
		c.width = this.width * SCALE;
		c.height = this.height * SCALE;
		const ctx = c.getContext('2d')!;
		ctx.imageSmoothingEnabled = false;

		const isCity = this.mode === 'city';

		// 1. Background (Nature vs City)
		ctx.fillStyle = isCity ? 'rgb(230, 220, 200)' : 'rgb(34, 54, 24)';
		ctx.fillRect(0, 0, c.width, c.height);

		// 2. Ground Textures (Roads & Squares) - optimized with single pass
		const roadColor1 = isCity ? '#7a7056' : '#422e1a';
		const roadColor2 = isCity ? '#857a5e' : '#4d3726';
		const squareColor = '#bebebe';

		for (let y = 0; y < this.height; y++) {
			for (let x = 0; x < this.width; x++) {
				const t = this.grid[y * this.width + x];
				if (t === TILE_ROAD) {
					ctx.fillStyle = (x + y) % 2 === 0 ? roadColor1 : roadColor2;
					ctx.fillRect(x * SCALE, y * SCALE, SCALE, SCALE);
				} else if (t === TILE_SQUARE) {
					ctx.fillStyle = squareColor;
					ctx.fillRect(x * SCALE, y * SCALE, SCALE, SCALE);
				}
			}
		}

		// 3. Street/Path Overlays (Vector pass)
		ctx.lineCap = 'round';
		ctx.lineJoin = 'round';

		// Regular paths
		ctx.lineWidth = Number(SCALE);
		ctx.strokeStyle = isCity ? 'rgb(170, 170, 170)' : 'rgb(65, 45, 30)';
		ctx.beginPath();
		for (const streetEdge of this.streetEdges) {
			const n1 = this.streetNodes[streetEdge.p1];
			const n2 = this.streetNodes[streetEdge.p2];
			if (!n1.isMain && !n2.isMain) {
				ctx.moveTo(n1.x * SCALE, n1.y * SCALE);
				ctx.lineTo(n2.x * SCALE, n2.y * SCALE);
			}
		}

		ctx.stroke();

		// Main roads
		ctx.lineWidth = 2 * SCALE;
		ctx.strokeStyle = isCity ? 'rgb(140, 140, 140)' : 'rgb(85, 60, 40)';
		ctx.beginPath();
		for (const streetEdge of this.streetEdges) {
			const n1 = this.streetNodes[streetEdge.p1];
			const n2 = this.streetNodes[streetEdge.p2];
			if (n1.isMain || n2.isMain) {
				ctx.moveTo(n1.x * SCALE, n1.y * SCALE);
				ctx.lineTo(n2.x * SCALE, n2.y * SCALE);
			}
		}

		ctx.stroke();

		// 4. Walls & Towers (City only or rocky barriers in nature)
		for (const wallNodes of this.walls) {
			ctx.lineWidth = 2 * SCALE;
			ctx.strokeStyle = isCity ? 'rgb(70, 70, 70)' : 'rgb(40, 40, 45)';
			ctx.beginPath();
			for (let i = 0; i < wallNodes.length; i++) {
				const p1 = wallNodes[i];
				const p2 = wallNodes[(i + 1) % wallNodes.length];
				ctx.moveTo(p1.x * SCALE, p1.y * SCALE);
				ctx.lineTo(p2.x * SCALE, p2.y * SCALE);
			}

			ctx.stroke();

			const towerRadius = (isCity ? 3 : 4) * SCALE;
			for (const p of wallNodes) {
				ctx.fillStyle = isCity ? 'rgb(90, 90, 90)' : 'rgb(50, 50, 55)';
				ctx.beginPath();
				ctx.arc(p.x * SCALE, p.y * SCALE, towerRadius, 0, Math.PI * 2);
				ctx.fill();
				if (isCity) {
					ctx.fillStyle = 'rgb(110, 110, 110)';
					ctx.beginPath();
					ctx.arc(p.x * SCALE, p.y * SCALE, towerRadius * 0.7, 0, Math.PI * 2);
					ctx.fill();
				}
			}
		}

		// 5. Houses / Trees (Pseudo-3D)
		for (let i = 0; i < this.houses.length; i++) {
			const h = this.houses[i];
			const cx = (h.x + h.w / 2) * SCALE;
			const cy = (h.y + h.h / 2) * SCALE;
			const pw = h.w * SCALE;
			const ph = h.h * SCALE;

			ctx.save();
			ctx.translate(cx, cy);
			if (isCity) {
				ctx.rotate(h.rotation);
				ctx.fillStyle = 'rgb(77, 55, 38)';
				ctx.fillRect(-pw / 2, -ph / 2, pw, ph);
				// Use deterministic pattern instead of random for roof color
				ctx.fillStyle = i % 10 < 7 ? 'rgb(143, 77, 54)' : 'rgb(122, 62, 41)';
				ctx.fillRect(-pw / 2, -ph / 2 - 4, pw, ph);
				ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
				ctx.strokeRect(-pw / 2, -ph / 2 - 4, pw, ph);
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

		return c;
	}

	private generateTraversability(): Uint8Array {
		const data = new Uint8Array(this.width * this.height);
		for (let i = 0; i < data.length; i++) {
			data[i] = this.grid[i] === TILE_HOUSE ? 0 : 255;
		}

		for (const wallNodes of this.walls) {
			for (let i = 0; i < wallNodes.length; i++) {
				const p1 = wallNodes[i];
				const p2 = wallNodes[(i + 1) % wallNodes.length];

				// Block Line
				const dist = Math.hypot(p2.x - p1.x, p2.y - p1.y);
				const steps = Math.ceil(dist * 2);
				for (let k = 0; k <= steps; k++) {
					const t = k / steps;
					const x = Math.floor(p1.x + (p2.x - p1.x) * t);
					const y = Math.floor(p1.y + (p2.y - p1.y) * t);
					this.blockCell(data, x, y, 1);
				}

				// Block Tower Area (Circular collision)
				const towerR = 3;
				for (let ty = -towerR; ty <= towerR; ty++) {
					for (let tx = -towerR; tx <= towerR; tx++) {
						if (tx * tx + ty * ty <= towerR * towerR) {
							this.blockCell(data, Math.floor(p1.x + tx), Math.floor(p1.y + ty), 0);
						}
					}
				}
			}
		}

		return data;
	}

	// ДОБАВЛЕННЫЙ ПРИВАТНЫЙ ХЕЛПЕР:
	private blockCell(data: Uint8Array, x: number, y: number, radius = 0) {
		for (let dy = -radius; dy <= radius; dy++) {
			for (let dx = -radius; dx <= radius; dx++) {
				const px = x + dx;
				const py = y + dy;
				if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
					data[py * this.width + px] = 0;
				}
			}
		}
	}

	// Helper to adapt to game engine's Pathfinding format
	public getTraversabilityData(): {
		width: number;
		height: number;
		data: Uint8Array;
		heightData: Uint8Array;
		roadData: Uint8Array;
		iceData: Uint8Array;
	} {
		const length = this.width * this.height;
		const data = this.generateTraversability(); // 0=blocked, 255=walkable

		const heightData = new Uint8Array(length).fill(128); // City is flat
		const roadData = new Uint8Array(length);
		const iceData = new Uint8Array(length).fill(0);

		// Populate road data for movement speed bonus
		for (let i = 0; i < length; i++) {
			roadData[i] = this.grid[i] === TILE_ROAD ? 255 : 0;
		}

		return {
			width: this.width,
			height: this.height,
			data,
			heightData,
			roadData,
			iceData,
		};
	}
}

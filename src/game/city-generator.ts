// === Organic City Generator ===
// Implements mycelium-like street growth, dynamic house placement, and wall generation.
export const TILE_EMPTY = 0;
export const TILE_ROAD = 1;
export const TILE_HOUSE = 2;
export const TILE_WALL = 3;
export const TILE_SQUARE = 6;

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
	p1: number; // index in nodes
	p2: number; // index in nodes
};

type House = {
	x: number;
	y: number;
	w: number;
	h: number;
	rotation: number;
};

export class CityGenerator {
	private width: number;
	private height: number;
	private seed: number;
	
	// 0=empty, 1=street, 2=house
	private grid: Uint8Array;
	
	private streetNodes: StreetNode[] = [];
	private streetEdges: StreetEdge[] = [];
	private houses: House[] = [];
	private walls: Array<Array<{x: number; y: number}>> = [];
	private wallsBuilt = new Set<number>();

	private centerX: number;
	private centerY: number;

	constructor(seed: number, width = 256, height = 256) { // Масштаб Masum
		this.seed = seed;
		this.width = width;
		this.height = height;
		this.grid = new Uint8Array(width * height);
		// Смещение центра для органичности (из deterministic_city.py)
		this.centerX = Math.floor(width / 2) + (this.random() > 0.5 ? this.randInt(-10, 10) : 0);
		this.centerY = Math.floor(height / 2) + (this.random() > 0.5 ? this.randInt(-10, 10) : 0);
	}

	// Pseudo-random number generator
	private random(): number {
		const x = Math.sin(this.seed++) * 10000;
		return x - Math.floor(x);
	}

	private randInt(min: number, max: number): number {
		return Math.floor(this.random() * (max - min + 1)) + min;
	}

	private randFloat(min: number, max: number): number {
		return this.random() * (max - min) + min;
	}

	public generate(population: number): CityMapData {
		// 1. Сначала стены, чтобы дороги знали, где входы (логика Python)
		this.precalculateWalls(population);
		// 2. Дороги ведут к воротам в стенах
		this.initializeMainRoadsThroughGates();
		this.generateCentralSquare(population);
		this.grow(population);

		
		const visual = this.render();
		const traversability = this.generateTraversability();

		return {
			visual,
			grid: traversability,
			width: this.width,
			height: this.height,
			spawnX: this.centerX,
			spawnY: this.centerY
		};
	}

	private precalculateWalls(population: number) {
		const thresholds = [1000, 10000];
		for (const t of thresholds) {
			if (population >= t) {
				const radius = t === 1000 ? this.width / 8 : this.width / 4;
				const segs = t === 1000 ? 6 : 8;
				this.buildWall(radius, segs);
				this.wallsBuilt.add(t);
			}
		}
	}

	private initializeMainRoadsThroughGates() {
		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true});
		if (this.walls.length === 0) {
			this.markLineOnGrid(this.centerX, this.centerY, 0, this.centerY, TILE_ROAD, 2);
			this.markLineOnGrid(this.centerX, this.centerY, this.width - 1, this.centerY, TILE_ROAD, 2);
			return;
		}
		const outerWall = this.walls[this.walls.length - 1];
		for (let i = 0; i < outerWall.length; i += 2) {
			const p1 = outerWall[i];
			const p2 = outerWall[(i + 1) % outerWall.length];
			const gateX = Math.floor((p1.x + p2.x) / 2);
			const gateY = Math.floor((p1.y + p2.y) / 2);
			const dx = gateX - this.centerX;
			const dy = gateY - this.centerY;
			const dist = Math.hypot(dx, dy);
			const unitX = dx / dist;
			const unitY = dy / dist;
			this.markLineOnGrid(this.centerX, this.centerY, this.centerX + unitX * this.width, this.centerY + unitY * this.width, TILE_ROAD, 2);
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
					if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
						// Используем константу TILE_HOUSE
						if (this.grid[py * this.width + px] !== TILE_HOUSE) {
							this.grid[py * this.width + px] = value;
						}
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
				if (overlap) break;
			}

			if (overlap) {
				toRemove.push(i);
				// Clear grid
				for (let hy = h.y; hy < h.y + h.h; hy++) {
					for (let hx = h.x; hx < h.x + h.w; hx++) {
						if (hx >= 0 && hx < this.width && hy >= 0 && hy < this.height) {
							if (this.grid[hy * this.width + hx] === 2) {
								this.grid[hy * this.width + hx] = 0;
							}
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
			this.grid[idx] = 1;
		}

		return toRemove.length;
	}

	private growStreetBranch(): {p1: number; p2: number} | null {
		if (this.streetNodes.length < 5) return null;

		// Pick random node (exclude first 5 main roads if possible)
		const startIdx = this.streetNodes.length > 5 ? 5 : 0;
		const parentId = this.randInt(startIdx, this.streetNodes.length - 1);
		const parent = this.streetNodes[parentId];

		// Try angles
		for (let i = 0; i < 12; i++) {
			const angle = this.randFloat(0, Math.PI * 2);
			const dist = this.randFloat(10, 20); // Length 10-20
			
			const nx = parent.x + Math.cos(angle) * dist;
			const ny = parent.y + Math.sin(angle) * dist;

			const margin = 64; 
			if (nx < margin || nx >= this.width - margin || ny < margin || ny >= this.height - margin) continue;

			// Proximity check to existing nodes
			let tooClose = false;
			for (const node of this.streetNodes) {
				if (Math.hypot(node.x - nx, node.y - ny) < 8) {
					tooClose = true;
					break;
				}
			}
			if (tooClose) continue;

			// Accept new node
			const newNode = {x: nx, y: ny, isMain: false};
			const newId = this.streetNodes.length;
			this.streetNodes.push(newNode);
			this.streetEdges.push({p1: parentId, p2: newId});

			// Handle houses
			const removed = this.markStreetAndRemoveHouses(parent.x, parent.y, nx, ny);
			
			// Compensate
			for (let k = 0; k < removed; k++) this.tryPlaceHouse();
			
			// Densify
			for (let k = 0; k < 10; k++) this.tryPlaceHouse({p1: parentId, p2: newId});

			return {p1: parentId, p2: newId};
		}

		return null;
	}

	private tryPlaceHouse(edge?: StreetEdge): boolean {
		if (this.streetEdges.length === 0) return false;

		// Pick edge
		let e = edge;
		if (!e) {
			// Pick random REGULAR edge (not main)
			const regular = this.streetEdges.filter(ed => 
				!this.streetNodes[ed.p1].isMain || !this.streetNodes[ed.p2].isMain
			);
			if (regular.length === 0) return false;
			e = regular[this.randInt(0, regular.length - 1)];
		}

		const n1 = this.streetNodes[e.p1];
		const n2 = this.streetNodes[e.p2];

		// Try placement
		for (let attempt = 0; attempt < 60; attempt++) {
			const t = this.randFloat(0, 1);
			const sx = n1.x + (n2.x - n1.x) * t;
			const sy = n1.y + (n2.y - n1.y) * t;

			const side = this.random() > 0.5 ? 1 : -1;
			const dx = n2.x - n1.x;
			const dy = n2.y - n1.y;
			const len = Math.hypot(dx, dy);
			if (len < 0.1) continue;

			// Perpendicular placement (Faithful port from grape_city.py try_place_house)
			const perpX = -dy / len;
			const perpY = dx / len;

			// Дистанция от дороги 2-4 клетки для плотной застройки
			const distance = this.randInt(2, 4);
			const hx = Math.floor(sx + perpX * distance * side);
			const hy = Math.floor(sy + perpY * distance * side);
			
			const w = this.randInt(2, 4);
			const h = this.randInt(2, 4);

			// Check bounds
			if (hx < 5 || hx >= this.width - 5 || hy < 5 || hy >= this.height - 5) continue;
			
			// Check occupancy
			let free = true;
			for (let y = hy; y < hy + h; y++) {
				for (let x = hx; x < hx + w; x++) {
					if (this.grid[y * this.width + x] !== 0) {
						free = false;
						break;
					}
				}
				if (!free) break;
			}

			if (free) {
				// Place
				for (let y = hy; y < hy + h; y++) {
					for (let x = hx; x < hx + w; x++) {
						this.grid[y * this.width + x] = 2;
					}
				}
				const angle = Math.atan2(dy, dx) + (this.randFloat(-0.3, 0.3));
				this.houses.push({x: hx, y: hy, w, h, rotation: angle});
				return true;
			}
		}
		return false;
	}
	// НОВЫЕ И ОБНОВЛЕННЫЕ МЕТОДЫ:

	private precalculateWalls(population: number) {
		const thresholds = [1000, 10000];
		for (const t of thresholds) {
			if (population >= t) {
				const radius = t === 1000 ? this.width / 8 : this.width / 4;
				const segs = t === 1000 ? 6 : 8;
				this.buildWall(radius, segs);
				this.wallsBuilt.add(t);
			}
		}
	}

	private initializeMainRoadsThroughGates() {
		if (this.walls.length === 0) {
			// Если стен нет (малый город), рисуем стандартный крест
			this.markLineOnGrid(this.centerX, this.centerY, 0, this.centerY, 1, 2);
			this.markLineOnGrid(this.centerX, this.centerY, this.width-1, this.centerY, 1, 2);
			return;
		}

		// Берем внешнюю стену (последнюю в массиве)
		const outerWall = this.walls[this.walls.length - 1];
		
		// Находим ворота (середины сегментов стен) как в city_map_generator.py
		for (let i = 0; i < outerWall.length; i += 2) { // Берем через один сегмент для 4-х направлений
			const p1 = outerWall[i];
			const p2 = outerWall[(i + 1) % outerWall.length];
			
			const gateX = Math.floor((p1.x + p2.x) / 2);
			const gateY = Math.floor((p1.y + p2.y) / 2);

			// Дорога от края карты через ворота к центру
			// Вычисляем вектор от центра к воротам и продлеваем его
			const dx = gateX - this.centerX;
			const dy = gateY - this.centerY;
			const dist = Math.hypot(dx, dy);
			
			const unitX = dx / dist;
			const unitY = dy / dist;

			// Рисуем главную дорогу (ширина 2)
			this.markLineOnGrid(this.centerX, this.centerY, this.centerX + unitX * this.width, this.centerY + unitY * this.width, 1, 2);
		}
	}

	// Реализация "House Compensation" из grape_city.py
	private grow(targetPop: number) {
		const targetStreets = Math.min(800, Math.floor(Math.sqrt(targetPop)));
		const targetHouses = Math.floor(Math.pow(targetPop, 0.8));

		let housesAdded = this.houses.length;
		const maxIter = targetHouses * 50;
		let iter = 0;

		while (housesAdded < targetHouses && iter < maxIter) {
			iter++;
			
			// Растим ветку улицы
			const newEdge = this.growStreetBranch();
			if (newEdge) {
				const n1 = this.streetNodes[newEdge.p1];
				const n2 = this.streetNodes[newEdge.p2];
				
				// ФИШКА ПИТОНА: Улица может удалить дом. Считаем сколько удалили.
				const removed = this.markStreetAndRemoveHouses(n1.x, n1.y, n2.x, n2.y);
				
				// Уменьшаем счетчик, чтобы генератор восполнил потерю
				housesAdded -= removed;
			}

			// Пытаемся ставить дома, пока не достигнем цели
			let fails = 0;
			while (fails < 100 && housesAdded < targetHouses) {
				if (this.tryPlaceHouse()) {
					housesAdded++;
					fails = 0;
				} else {
					fails++;
				}
			}
		}
	}
	private getCenterOfMass(): {x: number; y: number} {
		if (this.streetNodes.length === 0) return {x: this.width/2, y: this.height/2};
		
		let sumX = 0;
		let sumY = 0;
		for (const n of this.streetNodes) {
			sumX += n.x;
			sumY += n.y;
		}
		return {
			x: sumX / this.streetNodes.length,
			y: sumY / this.streetNodes.length
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
				y: center.y + Math.sin(angle) * r
			});
		}
		this.walls.push(nodes);
	}

	private grow(targetPop: number) {
		const targetStreets = Math.min(800, Math.floor(Math.sqrt(targetPop)));
		const targetHouses = Math.floor(Math.pow(targetPop, 0.8));

		let housesAdded = this.houses.length;
		const maxIter = Math.max(targetHouses, targetStreets) * 50;
		let iter = 0;

		while (housesAdded < targetHouses && iter < maxIter) {
			iter++;
			// Grow street
			if (this.growStreetBranch()) {
				// street added
			}

			// Place houses
			let fails = 0;
			const maxFails = 120;
			while (fails < maxFails && housesAdded < targetHouses) {
				if (this.tryPlaceHouse()) {
					housesAdded++;
					fails = 0;
					
					// Wall check
					const thresholds = [1000, 10000];
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
		c.width = this.width * 4; // 4x upscale for crisp pixel art look
		c.height = this.height * 4;
		const ctx = c.getContext('2d')!;
		
		// Background (sand/dirt color from python script: 230, 220, 200)
		ctx.fillStyle = 'rgb(230, 220, 200)';
		ctx.fillRect(0, 0, c.width, c.height);

		const SCALE = 4;

		// Draw streets
		ctx.lineWidth = 1 * SCALE;
		ctx.strokeStyle = 'rgb(170, 170, 170)';
		ctx.lineCap = 'round';
		
		// Regular streets
		ctx.beginPath();
		for (const e of this.streetEdges) {
			const n1 = this.streetNodes[e.p1];
			const n2 = this.streetNodes[e.p2];
			if (!n1.isMain && !n2.isMain) {
				ctx.moveTo(n1.x * SCALE, n1.y * SCALE);
				ctx.lineTo(n2.x * SCALE, n2.y * SCALE);
			}
		}
		ctx.stroke();

		// Main streets
		ctx.lineWidth = 2 * SCALE;
		ctx.strokeStyle = 'rgb(140, 140, 140)';
		ctx.beginPath();
		for (const e of this.streetEdges) {
			const n1 = this.streetNodes[e.p1];
			const n2 = this.streetNodes[e.p2];
			if (n1.isMain || n2.isMain) {
				ctx.moveTo(n1.x * SCALE, n1.y * SCALE);
				ctx.lineTo(n2.x * SCALE, n2.y * SCALE);
			}
		}
		ctx.stroke();

		// Houses
		for (const h of this.houses) {
			const cx = (h.x + h.w / 2) * SCALE;
			const cy = (h.y + h.h / 2) * SCALE;
			const pw = h.w * SCALE;
			const ph = h.h * SCALE;

			ctx.save();
			ctx.translate(cx, cy);
			ctx.rotate(h.rotation);
			
			// 1. Shadow/Wall (lower part)
			ctx.fillStyle = 'rgb(77, 55, 38)'; // Dark wood/wall
			ctx.fillRect(-pw/2, -ph/2, pw, ph);

			// 2. Roof (Top part, slightly offset up to create height)
			// Мы сдвигаем крышу вверх на 4 пикселя
			ctx.fillStyle = this.random() < 0.7 ? 'rgb(143, 77, 54)' : 'rgb(122, 62, 41)'; // Clay colors
			ctx.fillRect(-pw/2, -ph/2 - 4, pw, ph);

			// 3. Roof Ridge (Highlight line on top)
			ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
			ctx.lineWidth = 1;
			ctx.strokeRect(-pw/2, -ph/2 - 4, pw, ph);
			
			ctx.restore();
		}

		// Walls
		// БЫЛО (в методе render - отрисовка стен):
		// Walls
		ctx.lineWidth = 2 * SCALE;
		ctx.strokeStyle = 'rgb(70, 70, 70)';
		for (const wall of this.walls) {
			ctx.beginPath();
			for (let i = 0; i < wall.length; i++) {
				const p1 = wall[i];
				const p2 = wall[(i + 1) % wall.length];
				ctx.moveTo(p1.x * SCALE, p1.y * SCALE);
				ctx.lineTo(p2.x * SCALE, p2.y * SCALE);
			}
			ctx.stroke();

			// Towers
			ctx.fillStyle = 'rgb(90, 90, 90)';
			const towerR = 3 * SCALE;
			for (const p of wall) {
				ctx.beginPath();
				ctx.arc(p.x * SCALE, p.y * SCALE, towerR, 0, Math.PI * 2);
				ctx.fill();
			}
		}

// СТАЛО:
		// Walls (Faithful port from grape_city.py / city_map_generator.py)
		const SCALE = 4;
		for (const wallNodes of this.walls) {
			// 1. Draw Wall Segments (Lines)
			ctx.lineWidth = 2 * SCALE;
			ctx.strokeStyle = 'rgb(70, 70, 70)';
			ctx.lineJoin = 'round';
			ctx.beginPath();
			for (let i = 0; i < wallNodes.length; i++) {
				const p1 = wallNodes[i];
				const p2 = wallNodes[(i + 1) % wallNodes.length];
				ctx.moveTo(p1.x * SCALE, p1.y * SCALE);
				ctx.lineTo(p2.x * SCALE, p2.y * SCALE);
			}
			ctx.stroke();

			// 2. Draw Circular Towers at nodes (node logic from Python)
			ctx.fillStyle = 'rgb(90, 90, 90)';
			const towerRadius = 3 * SCALE; // tower_r = cell_size * 3 из deterministic_city.py
			for (const p of wallNodes) {
				ctx.beginPath();
				ctx.arc(p.x * SCALE, p.y * SCALE, towerRadius, 0, Math.PI * 2);
				ctx.fill();
				// Tower detail (top)
				ctx.fillStyle = 'rgb(110, 110, 110)';
				ctx.beginPath();
				ctx.arc(p.x * SCALE, p.y * SCALE, towerRadius * 0.7, 0, Math.PI * 2);
				ctx.fill();
				ctx.fillStyle = 'rgb(90, 90, 90)'; // Reset
			}
		}

		return c;
	}

	private generateTraversability(): Uint8Array {
		// Map grid to 0 (blocked) or 255 (traversable)
		// Houses (2) are blocked.
		// Walls are not explicitly in grid, need to rasterize them for collision.
		
		const data = new Uint8Array(this.width * this.height);
		
		// 1. Copy grid: 0 (empty) -> 255, 1 (street) -> 255, 2 (house) -> 0
		for (let i = 0; i < data.length; i++) {
			data[i] = this.grid[i] === 2 ? 0 : 255;
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

				return data;
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
		const len = this.width * this.height;
		const data = this.generateTraversability(); // 0=blocked, 255=walkable
		
		const heightData = new Uint8Array(len).fill(128); // City is flat
		const roadData = new Uint8Array(len);
		const iceData = new Uint8Array(len).fill(0);

		// Populate road data for movement speed bonus
		for (let i = 0; i < len; i++) {
			// In internal grid: 1 = street.
			// Traversability 'data' has 255 for walkable.
			// We check internal grid for road flag.
			if (this.grid[i] === 1) {
				roadData[i] = 255; // Max road influence
			} else {
				roadData[i] = 0;
			}
		}

		return {
			width: this.width,
			height: this.height,
			data,
			heightData,
			roadData,
			iceData
		};
	}
}

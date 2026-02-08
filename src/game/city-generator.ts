// === Organic City Generator ===
// Implements mycelium-like street growth, dynamic house placement, and wall generation.

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

	constructor(seed: number, width = 1024, height = 1024) {
		this.seed = seed;
		this.width = width;
		this.height = height;
		this.grid = new Uint8Array(width * height);
		this.centerX = Math.floor(width / 2);
		this.centerY = Math.floor(height / 2);
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
		this.initializeMainRoads();
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

	private initializeMainRoads() {
		// Center node
		this.streetNodes.push({x: this.centerX, y: this.centerY, isMain: true}); // 0

		// Edge nodes
		const edges = [
			{x: 0, y: this.centerY}, 
			{x: this.width - 1, y: this.centerY},
			{x: this.centerX, y: 0},
			{x: this.centerX, y: this.height - 1}
		];

		for (const e of edges) {
			const id = this.streetNodes.length;
			this.streetNodes.push({...e, isMain: true});
			this.streetEdges.push({p1: 0, p2: id});
		}

		// Mark main roads on grid
		for (let i = 1; i <= 4; i++) {
			const n1 = this.streetNodes[0];
			const n2 = this.streetNodes[i];
			this.markLineOnGrid(n1.x, n1.y, n2.x, n2.y, 1, 2); // width 2 for main roads
		}
	}

	private markLineOnGrid(x1: number, y1: number, x2: number, y2: number, value: number, width = 1) {
		const dist = Math.hypot(x2 - x1, y2 - y1);
		const steps = Math.ceil(dist * 1.5);
		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			const x = Math.floor(x1 + (x2 - x1) * t);
			const y = Math.floor(y1 + (y2 - y1) * t);
			
			const hw = Math.floor(width / 2);
			for (let dy = -hw; dy <= hw; dy++) {
				for (let dx = -hw; dx <= hw; dx++) {
					const px = x + dx;
					const py = y + dy;
					if (px >= 0 && px < this.width && py >= 0 && py < this.height) {
						// Don't overwrite houses (2) with streets (1)
						if (this.grid[py * this.width + px] !== 2) {
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

			// Bounds check (margin 15)
			if (nx < 15 || nx >= this.width - 15 || ny < 15 || ny >= this.height - 15) continue;

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

			const perpX = -dy / len;
			const perpY = dx / len;

			const dist = this.randInt(2, 4);
			const hx = Math.floor(sx + perpX * dist * side);
			const hy = Math.floor(sy + perpY * dist * side);
			
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
			
			// Color variance
			ctx.fillStyle = this.random() < 0.7 ? 'rgb(210, 190, 160)' : 'rgb(200, 180, 150)';
			ctx.fillRect(-pw/2, -ph/2, pw, ph);
			
			ctx.restore();
		}

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

		// 2. Rasterize walls as blocked (0)
		for (const wall of this.walls) {
			for (let i = 0; i < wall.length; i++) {
				const p1 = wall[i];
				const p2 = wall[(i + 1) % wall.length];
				
				const dist = Math.hypot(p2.x - p1.x, p2.y - p1.y);
				const steps = Math.ceil(dist * 2);
				for (let k = 0; k <= steps; k++) {
					const t = k / steps;
					const x = Math.floor(p1.x + (p2.x - p1.x) * t);
					const y = Math.floor(p1.y + (p2.y - p1.y) * t);
					// Wall thickness 2
					for (let dy = -1; dy <= 1; dy++) {
						for (let dx = -1; dx <= 1; dx++) {
							const idx = (y + dy) * this.width + (x + dx);
							if (idx >= 0 && idx < data.length) data[idx] = 0;
						}
					}
				}
			}
		}

		return data;
	}
}

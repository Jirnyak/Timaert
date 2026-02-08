export const TILE_GRASS = 0;
export const TILE_ROAD = 1;
export const TILE_FLOOR = 2; // House floor
export const TILE_WALL = 3;
export const TILE_DOOR = 4;
export const TILE_WATER = 5;

type Rect = {x: number; y: number; w: number; h: number; rotation?: number};

export class CityGenerator {
	private grid: Uint8Array;
	private width: number;
	private height: number;
	private rng: () => number;
	
	// Growth state
	private streetNodes: Array<{x: number; y: number; isMain: boolean}> = [];
	private houses: Rect[] = [];
	private walls: Array<Array<{x: number; y: number}>> = [];
	private wallsBuilt = new Set<number>();

	constructor(
		seed: number,
		width = 256, // Grid size (tiles)
		height = 256
	) {
		this.width = width;
		this.height = height;
		this.grid = new Uint8Array(width * height).fill(TILE_GRASS);
		
		// LCG Random
		let s = seed;
		this.rng = () => {
			s = (s * 1664525 + 1013904223) % 4294967296;
			return s / 4294967296;
		};
	}

	// === Core Logic ===

	public generate(population: number): {
		grid: Uint8Array;
		width: number;
		height: number;
		visual: HTMLCanvasElement;
		spawnX: number;
		spawnY: number;
	} {
		this.initializeMainRoads();
		this.grow(population);
		
		// Create visual representation
		const canvas = this.renderToCanvas();
		
		return {
			grid: this.grid,
			width: this.width,
			height: this.height,
			visual: canvas,
			spawnX: Math.floor(this.width / 2),
			spawnY: Math.floor(this.height / 2)
		};
	}

	private initializeMainRoads() {
		const cx = this.width / 2;
		const cy = this.height / 2;

		// Main nodes: Center + 4 Edges
		this.streetNodes.push({x: cx, y: cy, isMain: true}); // 0
		const edges = [
			{x: 0, y: cy}, {x: this.width - 1, y: cy},
			{x: cx, y: 0}, {x: cx, y: this.height - 1}
		];

		for (const edge of edges) {
			this.streetNodes.push({...edge, isMain: true});
			this.drawStreet(cx, cy, edge.x, edge.y, 2);
		}
	}

	private grow(population: number) {
		// Heuristics: medieval cities ~100 people per hectare? 
		// We scale houses based on population.
		const targetHouses = Math.max(10, Math.pow(population, 0.65) * 2);
		let failures = 0;
		const MAX_FAILURES = 100;

		while (this.houses.length < targetHouses && failures < MAX_FAILURES) {
			// 1. Grow streets
			if (this.rng() < 0.3) {
				this.growStreetBranch();
			}

			// 2. Try place house
			if (this.tryPlaceHouse()) {
				failures = 0;
				this.checkWalls(this.houses.length * 5); // Approximate conversion houses->pop
			} else {
				failures++;
			}
		}
		
		// Ensure final walls
		this.checkWalls(population);
	}

	private growStreetBranch() {
		if (this.streetNodes.length === 0) return;

		// Pick random parent (prefer newer nodes)
		const parentIdx = Math.floor(this.rng() * this.streetNodes.length);
		const parent = this.streetNodes[parentIdx];

		// Try random direction
		const angle = this.rng() * Math.PI * 2;
		const len = 10 + this.rng() * 15;
		const nx = parent.x + Math.cos(angle) * len;
		const ny = parent.y + Math.sin(angle) * len;

		if (!this.inBounds(nx, ny, 5)) return;

		// Check density/too close
		for (const n of this.streetNodes) {
			const d = Math.hypot(n.x - nx, n.y - ny);
			if (d < 8) return; 
		}

		// Add node and draw
		this.streetNodes.push({x: nx, y: ny, isMain: false});
		this.drawStreet(parent.x, parent.y, nx, ny, 1);
	}

	private tryPlaceHouse(): boolean {
		if (this.streetNodes.length < 2) return false;

		// Pick random street node to try near
		const n = this.streetNodes[Math.floor(this.rng() * this.streetNodes.length)];
		
		// Random offset
		const angle = this.rng() * Math.PI * 2;
		const dist = 4 + this.rng() * 6;
		const hx = Math.floor(n.x + Math.cos(angle) * dist);
		const hy = Math.floor(n.y + Math.sin(angle) * dist);
		const w = 3 + Math.floor(this.rng() * 3); // 3-5
		const h = 3 + Math.floor(this.rng() * 3);

		if (!this.inBounds(hx, hy, w + 1)) return false;

		// Check collision with existing
		if (!this.isAreaFree(hx, hy, w, h)) return false;

		// Place house
		this.fillArea(hx, hy, w, h, TILE_FLOOR);
		// Add walls around house? Simplified: just floor for now, walls are visual
		this.houses.push({x: hx, y: hy, w, h});
		return true;
	}

	private checkWalls(currentPop: number) {
		const thresholds = [1000, 5000, 15000];
		const radii = [this.width / 6, this.width / 3.5, this.width / 2.2];
		
		for (let i = 0; i < thresholds.length; i++) {
			const t = thresholds[i];
			if (currentPop >= t && !this.wallsBuilt.has(t)) {
				this.buildCityWall(radii[i]);
				this.wallsBuilt.add(t);
			}
		}
	}

	private buildCityWall(radius: number) {
		const segments = 8 + Math.floor(this.rng() * 4);
		const points: Array<{x: number; y: number}> = [];
		const cx = this.width / 2;
		const cy = this.height / 2;

		for (let i = 0; i < segments; i++) {
			const theta = (i / segments) * Math.PI * 2;
			const r = radius * (0.9 + this.rng() * 0.2); // Organic variance
			points.push({
				x: cx + Math.cos(theta) * r,
				y: cy + Math.sin(theta) * r
			});
		}

		// Rasterize wall
		for (let i = 0; i < segments; i++) {
			const p1 = points[i];
			const p2 = points[(i + 1) % segments];
			this.drawWallLine(p1.x, p1.y, p2.x, p2.y);
		}
		this.walls.push(points);
	}

	// === Rasterization Helpers ===

	private drawStreet(x1: number, y1: number, x2: number, y2: number, width: number) {
		const dist = Math.hypot(x2 - x1, y2 - y1);
		const steps = Math.ceil(dist * 1.5);
		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			const x = Math.floor(x1 + (x2 - x1) * t);
			const y = Math.floor(y1 + (y2 - y1) * t);
			
			// Brush width
			for (let dy = -Math.floor(width/2); dy <= Math.ceil(width/2); dy++) {
				for (let dx = -Math.floor(width/2); dx <= Math.ceil(width/2); dx++) {
					this.setTile(x + dx, y + dy, TILE_ROAD);
				}
			}
		}
	}

	private drawWallLine(x1: number, y1: number, x2: number, y2: number) {
		const dist = Math.hypot(x2 - x1, y2 - y1);
		const steps = Math.ceil(dist * 2);
		for (let i = 0; i <= steps; i++) {
			const t = i / steps;
			const x = Math.floor(x1 + (x2 - x1) * t);
			const y = Math.floor(y1 + (y2 - y1) * t);
			// Wall is thick
			if (this.getTile(x, y) !== TILE_ROAD) { // Don't block main roads (simplified gate logic)
				this.setTile(x, y, TILE_WALL);
				this.setTile(x + 1, y, TILE_WALL);
				this.setTile(x, y + 1, TILE_WALL);
			}
		}
	}

	private inBounds(x: number, y: number, margin = 0): boolean {
		return x >= margin && y >= margin && x < this.width - margin && y < this.height - margin;
	}

	private isAreaFree(x: number, y: number, w: number, h: number): boolean {
		for (let dy = -1; dy <= h; dy++) {
			for (let dx = -1; dx <= w; dx++) {
				const t = this.getTile(x + dx, y + dy);
				if (t !== TILE_GRASS) return false;
			}
		}
		return true;
	}

	private fillArea(x: number, y: number, w: number, h: number, type: number) {
		for (let dy = 0; dy < h; dy++) {
			for (let dx = 0; dx < w; dx++) {
				this.setTile(x + dx, y + dy, type);
			}
		}
	}

	private getTile(x: number, y: number): number {
		if (!this.inBounds(x, y)) return -1;
		return this.grid[y * this.width + x];
	}

	private setTile(x: number, y: number, type: number) {
		if (this.inBounds(x, y)) {
			// Priorities: Wall > Floor > Road > Grass
			const current = this.grid[y * this.width + x];
			if (current === TILE_WALL) return; // Walls persist
			if (current === TILE_FLOOR && type === TILE_ROAD) return; // Houses block roads
			this.grid[y * this.width + x] = type;
		}
	}

	// === Rendering ===

	private renderToCanvas(): HTMLCanvasElement {
		const c = document.createElement('canvas');
		c.width = this.width * 4; // 4x upscale for crisp pixel art look if needed, or 1x
		c.height = this.height * 4;
		const ctx = c.getContext('2d')!;
		ctx.imageSmoothingEnabled = false;

		// 1. Background
		ctx.fillStyle = '#4a6b36'; // Grass dark
		ctx.fillRect(0, 0, c.width, c.height);

		// Helper to draw a pixel
		const drawPixel = (x: number, y: number, color: string) => {
			ctx.fillStyle = color;
			ctx.fillRect(x * 4, y * 4, 4, 4);
		};

		// 2. Iterate Grid
		for (let y = 0; y < this.height; y++) {
			for (let x = 0; x < this.width; x++) {
				const t = this.grid[y * this.width + x];
				if (t === TILE_ROAD) {
					// Noise for road
					drawPixel(x, y, this.rng() > 0.5 ? '#7a7056' : '#857a5e');
				} else if (t === TILE_FLOOR) {
					drawPixel(x, y, '#4d3726'); // Dark wood
				} else if (t === TILE_WALL) {
					drawPixel(x, y, '#6e7075'); // Stone
				}
			}
		}

		// 3. Draw House Roofs (Pseudo-3D effect)
		// We iterate houses and draw roofs slightly offset up
		for (const h of this.houses) {
			const rx = h.x * 4;
			const ry = h.y * 4;
			const rw = h.w * 4;
			const rh = h.h * 4;
			
			// Roof
			ctx.fillStyle = '#8f4d36'; // Clay
			// Pyramid shape simple
			ctx.fillRect(rx, ry - 4, rw, rh);
			ctx.fillStyle = '#7a3e29'; // Shadow side
			ctx.fillRect(rx + 2, ry - 4 + 2, rw - 4, rh - 4);
		}

		return c;
	}
}

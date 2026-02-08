/* eslint-disable @typescript-eslint/no-restricted-types */
export type City = {
	x: number;
	y: number;
	connections: number[];
};

export type Road = {
	from: number;
	to: number;
	x1: number;
	y1: number;
	x2: number;
	y2: number;
};

export type LayerParameters = {
	// Layer 1: Settlement
	numCities: number;
	minCityDistance: number;
	maxConnections: number;
	seed: number;

	// Layer 2: Mask
	roadWidth: number;
	cityRadius: number;

	// Layer 3: Master Terrain
	heightScale: number;
	moistureScale: number;
	temperatureVariation: number;
	tempMin: number; // °C
	tempMax: number; // °C
	roadFlattenHeight: number;
	heightOctaves: number;
	moistureOctaves: number;
	domainWarp: number;
	roadWarpIntensity: number; // How curvy the roads are
	settlementBlur: number; // How much to blur the settlement areas

	// Layer 4: Visual
	seaLevel: number;
	snowLevel: number;
	beachWidth: number;
};

export const defaultParameters: LayerParameters = {
	numCities: 100,
	minCityDistance: 0.05,
	maxConnections: 3,
	seed: 42,
	roadWidth: 0.01,
	cityRadius: 0.02,
	heightScale: 1,
	moistureScale: 1,
	temperatureVariation: 0.3,
	// Earth-ish range in °C
	tempMin: -25,
	tempMax: 35,
	roadFlattenHeight: 0.55,
	heightOctaves: 6,
	moistureOctaves: 4,
	domainWarp: 0.3,
	roadWarpIntensity: 0,
	settlementBlur: 0.05,
	seaLevel: 0.4,
	snowLevel: 0.8,
	beachWidth: 0.02,
};

export function createShader(gl: WebGL2RenderingContext, type: number, source: string): WebGLShader | null {
	const shader = gl.createShader(type);
	if (!shader) {
		return null;
	}

	gl.shaderSource(shader, source);
	gl.compileShader(shader);

	if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
		console.error('Shader compile error:', gl.getShaderInfoLog(shader));
		gl.deleteShader(shader);
		return null;
	}

	return shader;
}

export function createProgram(gl: WebGL2RenderingContext, vertexSource: string, fragmentSource: string): WebGLProgram | null {
	const vertexShader = createShader(gl, gl.VERTEX_SHADER, vertexSource);
	const fragmentShader = createShader(gl, gl.FRAGMENT_SHADER, fragmentSource);

	if (!vertexShader || !fragmentShader) {
		return null;
	}

	const program = gl.createProgram();
	if (!program) {
		return null;
	}

	gl.attachShader(program, vertexShader);
	gl.attachShader(program, fragmentShader);
	gl.linkProgram(program);

	if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
		console.error('Program link error:', gl.getProgramInfoLog(program));
		gl.deleteProgram(program);
		return null;
	}

	return program;
}

export function createFramebuffer(gl: WebGL2RenderingContext, width: number, height: number, format: number = gl.RGBA8): {framebuffer: WebGLFramebuffer; texture: WebGLTexture} | null {
	const texture = gl.createTexture();
	if (!texture) {
		return null;
	}

	gl.bindTexture(gl.TEXTURE_2D, texture);
	gl.texImage2D(gl.TEXTURE_2D, 0, format, width, height, 0, format === gl.R8 ? gl.RED : gl.RGBA, gl.UNSIGNED_BYTE, null);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);

	const framebuffer = gl.createFramebuffer();
	if (!framebuffer) {
		return null;
	}

	gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
	gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);

	gl.bindFramebuffer(gl.FRAMEBUFFER, null);

	return {framebuffer, texture};
}

export function createQuadBuffer(gl: WebGL2RenderingContext): WebGLBuffer | null {
	const buffer = gl.createBuffer();
	if (!buffer) {
		return null;
	}

	gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
	gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
		-1,
		-1,
		1,
		-1,
		-1,
		1,
		1,
		1,
	]), gl.STATIC_DRAW);

	return buffer;
}

// Seeded random number generator
function seededRandom(seed: number): () => number {
	let s = seed;
	return () => {
		s = (s * 1_103_515_245 + 12_345) & 0x7F_FF_FF_FF;
		return s / 0x7F_FF_FF_FF;
	};
}

// Calculate distance on a torus
function torusDistance(x1: number, y1: number, x2: number, y2: number): number {
	let dx = Math.abs(x2 - x1);
	let dy = Math.abs(y2 - y1);

	if (dx > 0.5) {
		dx = 1 - dx;
	}

	if (dy > 0.5) {
		dy = 1 - dy;
	}

	return Math.hypot(dx, dy);
}

// Union-Find data structure for connectivity check
class UnionFind {
	private parent: number[];
	private readonly rank: number[];

	constructor(size: number) {
		this.parent = Array.from({length: size}, (_, i) => i);
		this.rank = Array.from({length: size}, () => 0);
	}

	find(x: number): number {
		if (this.parent[x] !== x) {
			this.parent[x] = this.find(this.parent[x]);
		}

		return this.parent[x];
	}

	union(x: number, y: number): boolean {
		const rootX = this.find(x);
		const rootY = this.find(y);

		if (rootX === rootY) {
			return false;
		}

		if (this.rank[rootX] < this.rank[rootY]) {
			this.parent[rootX] = rootY;
		} else if (this.rank[rootX] > this.rank[rootY]) {
			this.parent[rootY] = rootX;
		} else {
			this.parent[rootY] = rootX;
			this.rank[rootX]++;
		}

		return true;
	}

	connected(x: number, y: number): boolean {
		return this.find(x) === this.find(y);
	}
}

// Generate cities with minimum distance constraint and fully connected graph
export function generateCities(parameters: LayerParameters): City[] {
	const random = seededRandom(parameters.seed);
	const cities: City[] = [];
	const maxAttempts = parameters.numCities * 100;
	let attempts = 0;

	// Step 1: Generate cities with distance constraint
	while (cities.length < parameters.numCities && attempts < maxAttempts) {
		attempts++;
		const x = random();
		const y = random();

		let tooClose = false;
		for (const city of cities) {
			const dist = torusDistance(x, y, city.x, city.y);
			if (dist < parameters.minCityDistance) {
				tooClose = true;
				break;
			}
		}

		if (!tooClose) {
			cities.push({x, y, connections: []});
		}
	}

	if (cities.length < 2) {
		return cities;
	}

	// Step 2: Create all edges with distances and sort them
	const edges: Array<{from: number; to: number; dist: number}> = [];
	for (let i = 0; i < cities.length; i++) {
		for (let j = i + 1; j < cities.length; j++) {
			const dist = torusDistance(cities[i].x, cities[i].y, cities[j].x, cities[j].y);
			edges.push({from: i, to: j, dist});
		}
	}

	edges.sort((a, b) => a.dist - b.dist);

	// Step 3: Build Minimum Spanning Tree using Kruskal's algorithm
	// This ensures ALL cities are connected with no isolated clusters
	const uf = new UnionFind(cities.length);
	const mstEdges: Array<{from: number; to: number; dist: number}> = [];

	for (const edge of edges) {
		if (uf.union(edge.from, edge.to)) {
			mstEdges.push(edge);
			// Add connection both ways
			if (!cities[edge.from].connections.includes(edge.to)) {
				cities[edge.from].connections.push(edge.to);
			}

			if (!cities[edge.to].connections.includes(edge.from)) {
				cities[edge.to].connections.push(edge.from);
			}

			// MST is complete when we have n-1 edges
			if (mstEdges.length === cities.length - 1) {
				break;
			}
		}
	}

	// Step 4: Add extra connections to nearest neighbors (beyond MST)
	// This creates a more realistic road network while keeping it connected
	for (let i = 0; i < cities.length; i++) {
		// Find all neighbors sorted by distance
		const neighbors: Array<{index: number; dist: number}> = [];
		for (let j = 0; j < cities.length; j++) {
			if (i === j) {
				continue;
			}

			const dist = torusDistance(cities[i].x, cities[i].y, cities[j].x, cities[j].y);
			neighbors.push({index: j, dist});
		}

		neighbors.sort((a, b) => a.dist - b.dist);

		// Add connections up to maxConnections
		let connectionCount = cities[i].connections.length;
		for (const neighbor of neighbors) {
			if (connectionCount >= parameters.maxConnections) {
				break;
			}

			const targetIdx = neighbor.index;
			if (!cities[i].connections.includes(targetIdx)) {
				cities[i].connections.push(targetIdx);
				connectionCount++;
				// Add reverse connection
				if (!cities[targetIdx].connections.includes(i)) {
					cities[targetIdx].connections.push(i);
				}
			}
		}
	}

	return cities;
}

// Get roads with proper wrapping
export function getRoads(cities: City[]): Road[] {
	const roads: Road[] = [];
	const added = new Set<string>();

	for (let i = 0; i < cities.length; i++) {
		for (const j of cities[i].connections) {
			const key = i < j ? `${i}-${j}` : `${j}-${i}`;
			if (added.has(key)) {
				continue;
			}

			added.add(key);

			const x1 = cities[i].x;
			const y1 = cities[i].y;
			const x2 = cities[j].x;
			const y2 = cities[j].y;

			// Find shortest path considering wrapping
			let dx = x2 - x1;
			let dy = y2 - y1;

			if (Math.abs(dx) > 0.5) {
				if (dx > 0) {
					dx -= 1;
				} else {
					dx += 1;
				}
			}

			if (Math.abs(dy) > 0.5) {
				if (dy > 0) {
					dy -= 1;
				} else {
					dy += 1;
				}
			}

			roads.push({
				from: i,
				to: j,
				x1,
				y1,
				x2: x1 + dx,
				y2: y1 + dy,
			});
		}
	}

	return roads;
}

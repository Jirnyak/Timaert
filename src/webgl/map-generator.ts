/* eslint-disable @typescript-eslint/no-restricted-types */
import {generateBiomeTexture} from '../game/biomes';
import {
	quadVertexShader,
	mainTerrainShader,
	visualTerrainShader,
	channelViewShader,
	roadVertexShader,
	roadFragmentShader,
	roadsViewShader,
	traversabilityShader,
	traversabilityViewShader,
} from './shaders';
import {
	type City,
	type Road,
	type LayerParameters,
	createProgram,
	createFramebuffer,
	createQuadBuffer,
	generateCities,
	getRoads,
} from './webgl-context';

export type ViewMode = 'visual' | 'height' | 'moisture' | 'temperature' | 'mask' | 'roads' | 'traversability';

// Traversability data for A* pathfinding
export type TraversabilityData = {
	width: number;
	height: number;
	data: Uint8Array; // 0 = not traversable, 255 = traversable
	heightData: Uint8Array; // Height for movement cost calculation
	roadData: Uint8Array; // Road influence (roads are faster to travel)
	iceData: Uint8Array; // Ice flag (>0 = frozen water)
};

export class MapGenerator {
	private readonly gl: WebGL2RenderingContext;
	private readonly width: number;
	private readonly height: number;

	// Textures and framebuffers
	private maskFB: {framebuffer: WebGLFramebuffer; texture: WebGLTexture} | null | undefined = null;
	private masterFB: {framebuffer: WebGLFramebuffer; texture: WebGLTexture} | null | undefined = null;
	private visualFB: {framebuffer: WebGLFramebuffer; texture: WebGLTexture} | null | undefined = null;
	private traversabilityFB: {framebuffer: WebGLFramebuffer; texture: WebGLTexture} | null | undefined = null;

	// Programs
	private roadProgram: WebGLProgram | null | undefined = null;
	private masterProgram: WebGLProgram | null | undefined = null;
	private visualProgram: WebGLProgram | null | undefined = null;
	private channelProgram: WebGLProgram | null | undefined = null;
	private roadsViewProgram: WebGLProgram | null | undefined = null;
	private traversabilityProgram: WebGLProgram | null | undefined = null;
	private traversabilityViewProgram: WebGLProgram | null | undefined = null;

	// Buffers
	private quadBuffer: WebGLBuffer | null = null;
	private roadBuffer: WebGLBuffer | null = null;
	private cityBuffer: WebGLBuffer | null = null;

	// Data
	private cities: City[] = [];
	private roads: Road[] = [];
	private params: LayerParameters;

	// CPU-uploaded textures
	private biomeTexture: WebGLTexture | null = null;
	private riverTexture: WebGLTexture | null = null;

	// Cached traversability data for CPU access
	private cachedTraversabilityData: TraversabilityData | null | undefined = null;

	constructor(canvas: HTMLCanvasElement, parameters: LayerParameters) {
		const gl = canvas.getContext('webgl2', {preserveDrawingBuffer: true});
		if (!gl) {
			throw new Error('WebGL2 not supported');
		}

		this.gl = gl;
		this.width = 1024;
		this.height = 1024;
		this.params = parameters;

		this.initShaders();
		this.initBuffers();
		this.initFramebuffers();
		this.initBiomeTexture();
	}

	private initShaders() {
		const {gl} = this;

		this.roadProgram = createProgram(gl, roadVertexShader, roadFragmentShader);
		this.masterProgram = createProgram(gl, quadVertexShader, mainTerrainShader);
		this.visualProgram = createProgram(gl, quadVertexShader, visualTerrainShader);
		this.channelProgram = createProgram(gl, quadVertexShader, channelViewShader);
		this.roadsViewProgram = createProgram(gl, quadVertexShader, roadsViewShader);
		this.traversabilityProgram = createProgram(gl, quadVertexShader, traversabilityShader);
		this.traversabilityViewProgram = createProgram(gl, quadVertexShader, traversabilityViewShader);
	}

	private initBuffers() {
		this.quadBuffer = createQuadBuffer(this.gl);
	}

	private initFramebuffers() {
		const {gl} = this;

		this.maskFB = createFramebuffer(gl, this.width, this.height);
		this.masterFB = createFramebuffer(gl, this.width, this.height);
		this.visualFB = createFramebuffer(gl, this.width, this.height);
		this.traversabilityFB = createFramebuffer(gl, this.width, this.height);
	}

	private initBiomeTexture() {
		const {gl} = this;
		const size = 64;
		const data = generateBiomeTexture(size);

		this.biomeTexture = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, this.biomeTexture);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, size, size, 0, gl.RGBA, gl.UNSIGNED_BYTE, data);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.bindTexture(gl.TEXTURE_2D, null);

		// Empty river texture (populated by generateRivers)
		this.uploadRiverTexture(new Uint8Array(this.width * this.height));
	}

	updateParams(parameters: LayerParameters) {
		this.params = parameters;
	}

	generateLayer1() {
		this.cities = generateCities(this.params);
		this.roads = getRoads(this.cities);
		this.updateRoadBuffers();
	}

	private updateRoadBuffers() {
		const {gl} = this;

		// Create thick line geometry for roads
		const roadVertices: number[] = [];
		const halfWidth = this.params.roadWidth;

		for (const road of this.roads) {
			// Calculate perpendicular direction for line thickness
			const dx = road.x2 - road.x1;
			const dy = road.y2 - road.y1;
			const length = Math.sqrt(dx * dx + dy * dy);
			const nx = -dy / length * halfWidth;
			const ny = dx / length * halfWidth;

			// Two triangles for each road segment
			roadVertices.push(road.x1 - nx, road.y1 - ny, road.x1 + nx, road.y1 + ny, road.x2 - nx, road.y2 - ny, road.x2 - nx, road.y2 - ny, road.x1 + nx, road.y1 + ny, road.x2 + nx, road.y2 + ny);
		}

		if (this.roadBuffer) {
			gl.deleteBuffer(this.roadBuffer);
		}

		this.roadBuffer = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, this.roadBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(roadVertices), gl.STATIC_DRAW);

		// Create circle geometry for cities
		const cityVertices: number[] = [];
		const segments = 16;

		for (const city of this.cities) {
			const r = this.params.cityRadius;
			for (let i = 0; i < segments; i++) {
				const a1 = (i / segments) * Math.PI * 2;
				const a2 = ((i + 1) / segments) * Math.PI * 2;

				cityVertices.push(city.x, city.y, city.x + Math.cos(a1) * r, city.y + Math.sin(a1) * r * 2, city.x + Math.cos(a2) * r, city.y + Math.sin(a2) * r * 2);
			}
		}

		if (this.cityBuffer) {
			gl.deleteBuffer(this.cityBuffer);
		}

		this.cityBuffer = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, this.cityBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(cityVertices), gl.STATIC_DRAW);
	}

	generateLayer2() {
		const {gl} = this;
		if (!this.maskFB || !this.roadProgram) {
			return;
		}

		gl.bindFramebuffer(gl.FRAMEBUFFER, this.maskFB.framebuffer);
		gl.viewport(0, 0, this.width, this.height);
		gl.clearColor(0, 0, 0, 1);
		gl.clear(gl.COLOR_BUFFER_BIT);

		gl.useProgram(this.roadProgram);

		const posLoc = gl.getAttribLocation(this.roadProgram, 'a_position');
		const offsetLoc = gl.getUniformLocation(this.roadProgram, 'u_offset');
		const scaleLoc = gl.getUniformLocation(this.roadProgram, 'u_scale');
		const intensityLoc = gl.getUniformLocation(this.roadProgram, 'u_intensity');

		gl.enableVertexAttribArray(posLoc);

		// Render 3x3 grid for seamless wrapping
		const offsets = [
			[-1, -1],
			[0, -1],
			[1, -1],
			[-1, 0],
			[0, 0],
			[1, 0],
			[-1, 1],
			[0, 1],
			[1, 1],
		];

		for (const [ox, oy] of offsets) {
			gl.uniform2f(offsetLoc, ox, oy);
			gl.uniform2f(scaleLoc, 1, 1);

			// Draw roads with gradient falloff
			gl.uniform1f(intensityLoc, 0.6);
			if (this.roadBuffer) {
				gl.bindBuffer(gl.ARRAY_BUFFER, this.roadBuffer);
				gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
				gl.drawArrays(gl.TRIANGLES, 0, this.roads.length * 6);
			}

			// Draw cities
			gl.uniform1f(intensityLoc, 1);
			if (this.cityBuffer) {
				gl.bindBuffer(gl.ARRAY_BUFFER, this.cityBuffer);
				gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
				gl.drawArrays(gl.TRIANGLES, 0, this.cities.length * 16 * 3);
			}
		}

		gl.bindFramebuffer(gl.FRAMEBUFFER, null);
	}

	generateLayer3() {
		const {gl} = this;
		if (!this.masterFB || !this.masterProgram || !this.maskFB) {
			return;
		}

		gl.bindFramebuffer(gl.FRAMEBUFFER, this.masterFB.framebuffer);
		gl.viewport(0, 0, this.width, this.height);

		gl.useProgram(this.masterProgram);

		// Set uniforms
		gl.uniform1i(gl.getUniformLocation(this.masterProgram, 'u_maskTexture'), 0);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_seed'), this.params.seed);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_heightScale'), this.params.heightScale);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_moistureScale'), this.params.moistureScale);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_temperatureVariation'), this.params.temperatureVariation);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_tempMin'), this.params.tempMin);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_tempMax'), this.params.tempMax);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_roadFlattenHeight'), this.params.roadFlattenHeight);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_heightOctaves'), this.params.heightOctaves);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_moistureOctaves'), this.params.moistureOctaves);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_domainWarp'), this.params.domainWarp);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_seaLevel'), this.params.seaLevel);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_roadWarpIntensity'), this.params.roadWarpIntensity);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_settlementBlur'), this.params.settlementBlur);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_continentScale'), this.params.continentScale);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_continentIntensity'), this.params.continentIntensity);
		gl.uniform1f(gl.getUniformLocation(this.masterProgram, 'u_ridgeIntensity'), this.params.ridgeIntensity);

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.maskFB.texture);

		// Draw full-screen quad
		const posLoc = gl.getAttribLocation(this.masterProgram, 'a_position');
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

		gl.bindFramebuffer(gl.FRAMEBUFFER, null);
	}

	generateLayer4() {
		const {gl} = this;
		if (!this.visualFB || !this.visualProgram || !this.masterFB || !this.maskFB) {
			return;
		}

		gl.bindFramebuffer(gl.FRAMEBUFFER, this.visualFB.framebuffer);
		gl.viewport(0, 0, this.width, this.height);

		gl.useProgram(this.visualProgram);

		gl.uniform1i(gl.getUniformLocation(this.visualProgram, 'u_masterTexture'), 0);
		gl.uniform1i(gl.getUniformLocation(this.visualProgram, 'u_maskTexture'), 1);
		gl.uniform1i(gl.getUniformLocation(this.visualProgram, 'u_biomeTexture'), 2);
		gl.uniform1i(gl.getUniformLocation(this.visualProgram, 'u_riverTexture'), 3);
		gl.uniform1f(gl.getUniformLocation(this.visualProgram, 'u_seaLevel'), this.params.seaLevel);
		gl.uniform1f(gl.getUniformLocation(this.visualProgram, 'u_snowLevel'), this.params.snowLevel);
		gl.uniform1f(gl.getUniformLocation(this.visualProgram, 'u_beachWidth'), this.params.beachWidth);
		gl.uniform1f(gl.getUniformLocation(this.visualProgram, 'u_tempMin'), this.params.tempMin);
		gl.uniform1f(gl.getUniformLocation(this.visualProgram, 'u_tempMax'), this.params.tempMax);
		gl.uniform1f(gl.getUniformLocation(this.visualProgram, 'u_riverMoistureBoost'), this.params.riverMoistureBoost);
		gl.uniform2f(gl.getUniformLocation(this.visualProgram, 'u_mapSize'), this.width, this.height);

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.masterFB.texture);
		gl.activeTexture(gl.TEXTURE1);
		gl.bindTexture(gl.TEXTURE_2D, this.maskFB.texture);
		gl.activeTexture(gl.TEXTURE2);
		gl.bindTexture(gl.TEXTURE_2D, this.biomeTexture);
		gl.activeTexture(gl.TEXTURE3);
		gl.bindTexture(gl.TEXTURE_2D, this.riverTexture);

		const posLoc = gl.getAttribLocation(this.visualProgram, 'a_position');
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

		gl.bindFramebuffer(gl.FRAMEBUFFER, null);
	}

	generateLayer5() {
		const {gl} = this;
		if (!this.traversabilityFB || !this.traversabilityProgram || !this.masterFB) {
			return;
		}

		gl.bindFramebuffer(gl.FRAMEBUFFER, this.traversabilityFB.framebuffer);
		gl.viewport(0, 0, this.width, this.height);

		gl.useProgram(this.traversabilityProgram);

		// Set uniforms
		gl.uniform1i(gl.getUniformLocation(this.traversabilityProgram, 'u_masterTexture'), 0);
		gl.uniform1f(gl.getUniformLocation(this.traversabilityProgram, 'u_seaLevel'), this.params.seaLevel);
		gl.uniform1f(gl.getUniformLocation(this.traversabilityProgram, 'u_maxTraversableHeight'), this.params.snowLevel - 0.05);
		gl.uniform1f(gl.getUniformLocation(this.traversabilityProgram, 'u_tempMin'), this.params.tempMin);
		gl.uniform1f(gl.getUniformLocation(this.traversabilityProgram, 'u_tempMax'), this.params.tempMax);

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.masterFB.texture);

		const posLoc = gl.getAttribLocation(this.traversabilityProgram, 'a_position');
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

		gl.bindFramebuffer(gl.FRAMEBUFFER, null);

		// Invalidate cached data
		this.cachedTraversabilityData = null;
	}

	// Get traversability data for CPU (for A* pathfinding)
	// This reads the GPU texture back to CPU memory
	getTraversabilityData(): TraversabilityData | null {
		if (this.cachedTraversabilityData) {
			return this.cachedTraversabilityData;
		}

		const {gl} = this;
		if (!this.traversabilityFB) {
			return null;
		}

		// Read pixels from traversability framebuffer
		gl.bindFramebuffer(gl.FRAMEBUFFER, this.traversabilityFB.framebuffer);

		const pixels = new Uint8Array(this.width * this.height * 4);
		gl.readPixels(0, 0, this.width, this.height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);

		gl.bindFramebuffer(gl.FRAMEBUFFER, null);

		// Extract channels into separate arrays for easier use
		const traversable = new Uint8Array(this.width * this.height);
		const heightData = new Uint8Array(this.width * this.height);
		const roadData = new Uint8Array(this.width * this.height);
		const iceData = new Uint8Array(this.width * this.height);

		for (let i = 0; i < this.width * this.height; i++) {
			traversable[i] = pixels[i * 4]; // R channel: traversability
			heightData[i] = pixels[i * 4 + 1]; // G channel: height
			roadData[i] = pixels[i * 4 + 2]; // B channel: road influence
			iceData[i] = pixels[i * 4 + 3]; // A channel: ice flag
		}

		this.cachedTraversabilityData = {
			width: this.width,
			height: this.height,
			data: traversable,
			heightData,
			roadData,
			iceData,
		};

		return this.cachedTraversabilityData;
	}

	// Check if a specific pixel is traversable
	isTraversable(x: number, y: number): boolean {
		const data = this.getTraversabilityData();
		if (!data) {
			return false;
		}

		// Wrap coordinates for torus topology
		x = ((x % data.width) + data.width) % data.width;
		y = ((y % data.height) + data.height) % data.height;

		const idx = y * data.width + x;
		return data.data[idx] > 127; // Threshold at 50%
	}

	/** Pre-resolved traversability check — avoids per-call getTraversabilityData(). */
	getTraversabilityLookup(): ((x: number, y: number) => boolean) | undefined {
		const data = this.getTraversabilityData();
		if (!data) {
			return undefined;
		}

		const {width: w, height: h, data: buf} = data;
		return (x: number, y: number): boolean => {
			x = ((x % w) + w) % w;
			y = ((y % h) + h) % h;
			return buf[y * w + x] > 127;
		};
	}

	// Get movement cost for A* (higher = slower)
	getMovementCost(x: number, y: number): number {
		const data = this.getTraversabilityData();
		if (!data) {
			return Infinity;
		}

		x = ((x % data.width) + data.width) % data.width;
		y = ((y % data.height) + data.height) % data.height;

		const idx = y * data.width + x;

		if (data.data[idx] < 127) {
			return Infinity;
		} // Not traversable

		const height = data.heightData[idx] / 255;
		const isRoad = data.roadData[idx] > 25; // ~10% threshold

		// Base cost
		let cost = 1;

		// Roads are faster to travel
		if (isRoad) {
			cost *= 0.5;
		}

		// Steeper terrain is slower (cost increases with height gradient)
		// This is a simplified version - full implementation would check neighbors
		cost *= 1 + height * 0.5;

		return cost;
	}

	// ── River generation ─────────────────────────────────────────────
	// Biome boundaries form natural Voronoi-like edges. A smooth
	// distance field from those edges creates a potential surface:
	// cost ∝ 1 + edgeDist², so A* paths hug boundaries via continuous
	// gradients instead of binary snapping. Chaikin subdivision then
	// converts the grid-aligned A* path into smooth curves.

	generateRivers() {
		const {gl} = this;
		if (!this.masterFB) {
			return;
		}

		const w = this.width;
		const h = this.height;
		const n = w * h;
		const seaLevel8 = Math.floor(this.params.seaLevel * 255);

		gl.bindFramebuffer(gl.FRAMEBUFFER, this.masterFB.framebuffer);
		const pixels = new Uint8Array(n * 4);
		gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
		gl.bindFramebuffer(gl.FRAMEBUFFER, null);

		const height = new Uint8Array(n);
		const moisture = new Uint8Array(n);
		const temporary = new Uint8Array(n);
		for (let i = 0; i < n; i++) {
			height[i] = pixels[i * 4];
			moisture[i] = pixels[i * 4 + 1];
			temporary[i] = pixels[i * 4 + 2];
		}

		// ── 1. Coarse biome classification (3 moist × 2 temp = 6 IDs)
		const biome = new Uint8Array(n);
		for (let i = 0; i < n; i++) {
			if (height[i] <= seaLevel8) {
				biome[i] = 255;
				continue;
			}

			biome[i] = Math.min(1, Math.floor(temporary[i] / 128)) * 3
				+ Math.min(2, Math.floor(moisture[i] / 86));
		}

		// ── 2. Edge distance field (BFS from biome boundaries) ─────
		// edgeDist = 0 on boundary, increases inward — smooth potential
		const edgeDist = new Uint16Array(n);
		edgeDist.fill(65_535);
		const edgeQueue: number[] = [];

		for (let y = 0; y < h; y++) {
			for (let x = 0; x < w; x++) {
				const idx = y * w + x;
				if (biome[idx] === 255) {
					continue;
				}

				const b = biome[idx];
				for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]] as const) {
					const ni = ((y + dy + h) % h) * w + ((x + dx + w) % w);
					if (biome[ni] !== b) {
						edgeDist[idx] = 0;
						edgeQueue.push(idx);
						break;
					}
				}
			}
		}

		let head = 0;
		while (head < edgeQueue.length) {
			const idx = edgeQueue[head++];
			const d = edgeDist[idx];
			if (d >= 15) {
				continue;
			}

			const bx = idx % w;
			const by = Math.floor(idx / w);
			for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]] as const) {
				const ni = ((by + dy + h) % h) * w + ((bx + dx + w) % w);
				if (edgeDist[ni] > d + 1 && biome[ni] !== 255) {
					edgeDist[ni] = d + 1;
					edgeQueue.push(ni);
				}
			}
		}

		// ── 3. Distance-to-water BFS ───────────────────────────────
		const waterDist = new Uint16Array(n);
		waterDist.fill(65_535);
		const waterQueue: number[] = [];
		for (let i = 0; i < n; i++) {
			if (height[i] <= seaLevel8) {
				waterDist[i] = 0;
				waterQueue.push(i);
			}
		}

		head = 0;
		while (head < waterQueue.length) {
			const idx = waterQueue[head++];
			const d = waterDist[idx];
			const bx = idx % w;
			const by = Math.floor(idx / w);
			for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]] as const) {
				const ni = ((by + dy + h) % h) * w + ((bx + dx + w) % w);
				if (waterDist[ni] > d + 1) {
					waterDist[ni] = d + 1;
					waterQueue.push(ni);
				}
			}
		}

		// ── 4. Pick sources: near edges, far from water, well-spaced
		const candidates: Array<{idx: number; wd: number}> = [];
		for (let i = 0; i < n; i++) {
			if (height[i] > seaLevel8 && edgeDist[i] <= 2 && waterDist[i] > 8) {
				candidates.push({idx: i, wd: waterDist[i]});
			}
		}

		candidates.sort((a, b) => b.wd - a.wd);

		const minSpacing = 30;
		const sources: number[] = [];
		const taken = new Uint8Array(n);
		for (const c of candidates) {
			if (sources.length >= 100) {
				break;
			}

			if (taken[c.idx]) {
				continue;
			}

			sources.push(c.idx);
			const sx = c.idx % w;
			const sy = Math.floor(c.idx / w);
			for (let dy = -minSpacing; dy <= minSpacing; dy++) {
				for (let dx = -minSpacing; dx <= minSpacing; dx++) {
					if (dx * dx + dy * dy > minSpacing * minSpacing) {
						continue;
					}

					taken[((sy + dy + h) % h) * w + ((sx + dx + w) % w)] = 1;
				}
			}
		}

		// ── 5. Trace → smooth → stamp ──────────────────────────────
		const riverMask = new Uint8Array(n);
		for (const src of sources) {
			const raw = this.traceToWater(src, edgeDist, waterDist, height, riverMask, seaLevel8, w, h);
			if (!raw || raw.length < 5) {
				continue;
			}

			const smooth = this.smoothPath(raw, w, h);
			this.stampPath(smooth, height, seaLevel8, riverMask, w, h);
		}

		// ── 7. Carve + upload ──────────────────────────────────────
		const carveH = Math.max(1, seaLevel8 - 3);
		for (let i = 0; i < n; i++) {
			if (riverMask[i] > 0 && height[i] > seaLevel8) {
				pixels[i * 4] = Math.min(pixels[i * 4], carveH);
			}
		}

		gl.bindTexture(gl.TEXTURE_2D, this.masterFB.texture);
		gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
		gl.bindTexture(gl.TEXTURE_2D, null);

		this.uploadRiverTexture(riverMask);
	}

	/**
	 * A* from source to nearest water (or existing river).
	 * Cost = 1 + edgeDist² — quadratic potential keeps paths near
	 * biome edges with smooth fall-off instead of a hard wall.
	 */
	private traceToWater(
		source: number, edgeDist: Uint16Array, waterDist: Uint16Array,
		height: Uint8Array, riverMask: Uint8Array, seaLevel8: number,
		w: number, h: number,
	): Array<[number, number]> | undefined {
		const dirs: ReadonlyArray<readonly [number, number]> = [
			[-1, 0],
			[1, 0],
			[0, -1],
			[0, 1],
			[-1, -1],
			[1, -1],
			[-1, 1],
			[1, 1],
		];

		const gScore = new Map<number, number>();
		const cameFrom = new Map<number, number>();
		const maxBuckets = 4096;
		const buckets: number[][] = Array.from({length: maxBuckets}, () => []);

		const enqueue = (idx: number, f: number) => {
			buckets[Math.min(maxBuckets - 1, Math.max(0, Math.trunc(f)))].push(idx);
		};

		gScore.set(source, 0);
		enqueue(source, waterDist[source]);

		let explored = 0;
		for (let b = 0; b < maxBuckets && explored < 60_000; b++) {
			const bkt = buckets[b];
			while (bkt.length > 0 && explored < 60_000) {
				const cur = bkt.pop()!;
				explored++;
				const g = gScore.get(cur);
				if (g === undefined) {
					continue;
				}

				// Reached water or existing river → reconstruct
				const done = height[cur] <= seaLevel8
					|| (cur !== source && riverMask[cur] > 0);
				if (done) {
					return this.buildPath(source, cur, cameFrom, w);
				}

				const cx = cur % w;
				const cy = Math.floor(cur / w);
				for (const [dx, dy] of dirs) {
					const ni = ((cy + dy + h) % h) * w + ((cx + dx + w) % w);
					const ed = Math.min(edgeDist[ni], 15);
					const cost = 1 + ed * ed;
					const ng = g + cost;
					const previous = gScore.get(ni);
					if (previous !== undefined && ng >= previous) {
						continue;
					}

					gScore.set(ni, ng);
					cameFrom.set(ni, cur);
					enqueue(ni, ng + waterDist[ni]);
				}
			}
		}

		return undefined;
	}

	/** Reconstruct A* path by walking the cameFrom map. */
	private buildPath(
		source: number, goal: number,
		cameFrom: Map<number, number>, w: number,
	): Array<[number, number]> {
		const path: Array<[number, number]> = [];
		let c = goal;
		while (c !== source) {
			path.push([c % w, Math.floor(c / w)]);
			const previous = cameFrom.get(c);
			if (previous === undefined) {
				break;
			}

			c = previous;
		}

		path.push([source % w, Math.floor(source / w)]);
		path.reverse();
		return path;
	}

	/**
	 * Chaikin corner-cutting subdivision (3 iterations).
	 * Each pass replaces every segment P0→P1 with two points at
	 * 25% and 75%, producing C1-smooth curves from the grid path.
	 */
	private smoothPath(path: Array<[number, number]>, w: number, h: number): Array<[number, number]> {
		let pts = path;
		for (let iter = 0; iter < 3; iter++) {
			const next: Array<[number, number]> = [pts[0]];
			for (let i = 0; i < pts.length - 1; i++) {
				const [x0, y0] = pts[i];
				const [x1, y1] = pts[i + 1];
				let dx = x1 - x0;
				let dy = y1 - y0;
				if (Math.abs(dx) > w / 2) {
					dx -= Math.sign(dx) * w;
				}

				if (Math.abs(dy) > h / 2) {
					dy -= Math.sign(dy) * h;
				}

				next.push([
					((x0 + 0.25 * dx) % w + w) % w,
					((y0 + 0.25 * dy) % h + h) % h,
				], [
					((x0 + 0.75 * dx) % w + w) % w,
					((y0 + 0.75 * dy) % h + h) % h,
				]);
			}

			next.push(pts.at(-1));
			pts = next;
		}

		return pts;
	}

	/** Stamp smoothed path with variable width (thin at source, wider at mouth). */
	private stampPath(
		path: Array<[number, number]>,
		height: Uint8Array, seaLevel8: number,
		output: Uint8Array, w: number, h: number,
	) {
		for (let i = 0; i < path.length; i++) {
			const t = i / path.length;
			const radius = t < 0.15 ? 0 : Math.floor(0.5 + t * 1.5);
			const px = Math.round(path[i][0]);
			const py = Math.round(path[i][1]);
			for (let dy = -radius; dy <= radius; dy++) {
				for (let dx = -radius; dx <= radius; dx++) {
					if (dx * dx + dy * dy > radius * radius) {
						continue;
					}

					const ni = ((py + dy + h) % h) * w + ((px + dx + w) % w);
					if (height[ni] > seaLevel8) {
						output[ni] = 255;
					}
				}
			}
		}
	}

	private uploadRiverTexture(data: Uint8Array) {
		const {gl} = this;

		if (this.riverTexture) {
			gl.deleteTexture(this.riverTexture);
		}

		this.riverTexture = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, this.riverTexture);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, this.width, this.height, 0, gl.RED, gl.UNSIGNED_BYTE, data);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
		gl.bindTexture(gl.TEXTURE_2D, null);
	}

	generateAll() {
		this.generateLayer1();
		this.generateLayer2();
		this.generateLayer3();
		this.generateRivers();
		this.generateLayer4();
		this.generateLayer5();
		this.relocateUnderwaterCities();
	}

	// After terrain generation, move any city that ended up underwater to nearby land
	private relocateUnderwaterCities() {
		const trav = this.getTraversabilityData();
		if (!trav) {
			return;
		}

		let relocated = false;
		for (const city of this.cities) {
			const px = Math.floor(city.x * this.width) % this.width;
			const py = Math.floor(city.y * this.height) % this.height;
			if (trav.data[py * this.width + px] > 127) {
				continue; // Already on land
			}

			const land = this.findNearestLand(trav.data, px, py);
			if (land) {
				city.x = land.x / this.width;
				city.y = land.y / this.height;
				relocated = true;
			}
		}

		if (relocated) {
			// Rebuild roads and regenerate terrain layers with updated positions
			this.roads = getRoads(this.cities);
			this.updateRoadBuffers();
			this.cachedTraversabilityData = undefined;
			this.generateLayer2();
			this.generateLayer3();
			this.generateRivers();
			this.generateLayer4();
			this.generateLayer5();
		}
	}

	private findNearestLand(data: Uint8Array, cx: number, cy: number): {x: number; y: number} | undefined {
		const maxR = 200;
		for (let r = 1; r <= maxR; r++) {
			for (let dx = -r; dx <= r; dx++) {
				for (const dy of [-r, r]) {
					const nx = ((cx + dx) % this.width + this.width) % this.width;
					const ny = ((cy + dy) % this.height + this.height) % this.height;
					if (data[ny * this.width + nx] > 127) {
						return {x: nx, y: ny};
					}
				}
			}

			for (let dy = -r + 1; dy < r; dy++) {
				for (const dx of [-r, r]) {
					const nx = ((cx + dx) % this.width + this.width) % this.width;
					const ny = ((cy + dy) % this.height + this.height) % this.height;
					if (data[ny * this.width + nx] > 127) {
						return {x: nx, y: ny};
					}
				}
			}
		}

		return undefined;
	}

	render(viewMode: ViewMode, canvasWidth: number, canvasHeight: number) {
		const {gl} = this;

		gl.viewport(0, 0, canvasWidth, canvasHeight);
		gl.clearColor(0, 0, 0, 1);
		gl.clear(gl.COLOR_BUFFER_BIT);

		// Special handling for roads view
		if (viewMode === 'roads') {
			if (!this.roadsViewProgram || !this.maskFB) {
				return;
			}

			gl.useProgram(this.roadsViewProgram);
			gl.uniform1i(gl.getUniformLocation(this.roadsViewProgram, 'u_texture'), 0);

			gl.activeTexture(gl.TEXTURE0);
			gl.bindTexture(gl.TEXTURE_2D, this.maskFB.texture);

			const posLoc = gl.getAttribLocation(this.roadsViewProgram, 'a_position');
			gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
			gl.enableVertexAttribArray(posLoc);
			gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
			gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
			return;
		}

		// Special handling for traversability view
		if (viewMode === 'traversability') {
			if (!this.traversabilityViewProgram || !this.traversabilityFB) {
				return;
			}

			gl.useProgram(this.traversabilityViewProgram);
			gl.uniform1i(gl.getUniformLocation(this.traversabilityViewProgram, 'u_traversabilityTexture'), 0);

			gl.activeTexture(gl.TEXTURE0);
			gl.bindTexture(gl.TEXTURE_2D, this.traversabilityFB.texture);

			const posLoc = gl.getAttribLocation(this.traversabilityViewProgram, 'a_position');
			gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
			gl.enableVertexAttribArray(posLoc);
			gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
			gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
			return;
		}

		if (!this.channelProgram) {
			return;
		}

		gl.useProgram(this.channelProgram);

		let texture: WebGLTexture | null = null;
		let channel = 4; // RGB

		switch (viewMode) {
			case 'visual': {
				texture = this.visualFB?.texture ?? null;
				channel = 4;
				break;
			}

			case 'height': {
				texture = this.masterFB?.texture ?? null;
				channel = 0;
				break;
			}

			case 'moisture': {
				texture = this.masterFB?.texture ?? null;
				channel = 1;
				break;
			}

			case 'temperature': {
				texture = this.masterFB?.texture ?? null;
				channel = 2;
				break;
			}

			case 'mask': {
				texture = this.masterFB?.texture ?? null;
				channel = 3;
				break;
			}
		}

		if (!texture) {
			return;
		}

		gl.uniform1i(gl.getUniformLocation(this.channelProgram, 'u_texture'), 0);
		gl.uniform1i(gl.getUniformLocation(this.channelProgram, 'u_channel'), channel);

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, texture);

		const posLoc = gl.getAttribLocation(this.channelProgram, 'a_position');
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
	}

	getCities(): City[] {
		return this.cities;
	}

	getRoads(): Road[] {
		return this.roads;
	}

	getGL(): WebGL2RenderingContext {
		return this.gl;
	}

	getVisualTexture(): WebGLTexture | undefined {
		return this.visualFB?.texture;
	}

	getMasterTexture(): WebGLTexture | undefined {
		return this.masterFB?.texture;
	}

	getMapDimensions(): {width: number; height: number} {
		return {width: this.width, height: this.height};
	}

	destroy() {
		const {gl} = this;

		if (this.maskFB) {
			gl.deleteFramebuffer(this.maskFB.framebuffer);
			gl.deleteTexture(this.maskFB.texture);
		}

		if (this.masterFB) {
			gl.deleteFramebuffer(this.masterFB.framebuffer);
			gl.deleteTexture(this.masterFB.texture);
		}

		if (this.visualFB) {
			gl.deleteFramebuffer(this.visualFB.framebuffer);
			gl.deleteTexture(this.visualFB.texture);
		}

		if (this.traversabilityFB) {
			gl.deleteFramebuffer(this.traversabilityFB.framebuffer);
			gl.deleteTexture(this.traversabilityFB.texture);
		}

		if (this.biomeTexture) {
			gl.deleteTexture(this.biomeTexture);
		}

		if (this.riverTexture) {
			gl.deleteTexture(this.riverTexture);
		}

		if (this.quadBuffer) {
			gl.deleteBuffer(this.quadBuffer);
		}

		if (this.roadBuffer) {
			gl.deleteBuffer(this.roadBuffer);
		}

		if (this.cityBuffer) {
			gl.deleteBuffer(this.cityBuffer);
		}

		if (this.roadProgram) {
			gl.deleteProgram(this.roadProgram);
		}

		if (this.masterProgram) {
			gl.deleteProgram(this.masterProgram);
		}

		if (this.visualProgram) {
			gl.deleteProgram(this.visualProgram);
		}

		if (this.channelProgram) {
			gl.deleteProgram(this.channelProgram);
		}

		if (this.roadsViewProgram) {
			gl.deleteProgram(this.roadsViewProgram);
		}

		if (this.traversabilityProgram) {
			gl.deleteProgram(this.traversabilityProgram);
		}

		if (this.traversabilityViewProgram) {
			gl.deleteProgram(this.traversabilityViewProgram);
		}
	}
}

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

// Terrain data extracted from GPU for CPU-side gameplay
export type TerrainData = {
	width: number;
	height: number;
	heightData: Uint8Array; // Height for movement cost calculation
	roadData: Uint8Array; // Road influence (roads are faster to travel)
	iceData: Uint8Array; // Ice flag (>0 = frozen water)
	waterData: Uint8Array; // Non-traversable flag (0 = water/mountain, >0 = land)
	riverData: Uint8Array; // River mask (>0 = river cell)
	moistureData: Uint8Array; // Moisture 0-255 (dry→wet)
	temperatureData: Uint8Array; // Temperature 0-255 (cold→hot)
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

	// CPU river mask kept for terrain data readback
	private riverMask: Uint8Array | null = null;

	// Cached terrain data for CPU access
	private cachedTerrainData: TerrainData | null | undefined = null;

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
		this.cachedTerrainData = null;
	}

	// Get terrain data for CPU (height, roads, ice)
	// Reads the GPU traversability framebuffer back to CPU memory
	getTerrainData(): TerrainData | null {
		if (this.cachedTerrainData) {
			return this.cachedTerrainData;
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

		// Extract channels into separate arrays
		const heightData = new Uint8Array(this.width * this.height);
		const roadData = new Uint8Array(this.width * this.height);
		const iceData = new Uint8Array(this.width * this.height);
		const waterData = new Uint8Array(this.width * this.height);

		for (let i = 0; i < this.width * this.height; i++) {
			waterData[i] = pixels[i * 4]; // R channel: traversable (0 = water/mountain)
			heightData[i] = pixels[i * 4 + 1]; // G channel: height
			roadData[i] = pixels[i * 4 + 2]; // B channel: road influence
			iceData[i] = pixels[i * 4 + 3]; // A channel: ice flag
		}

		// Read climate data from master texture for biome-aware features
		const climate = this.getMasterClimateData();

		this.cachedTerrainData = {
			width: this.width,
			height: this.height,
			heightData,
			roadData,
			iceData,
			waterData,
			riverData: this.riverMask ?? new Uint8Array(this.width * this.height),
			moistureData: climate?.moisture ?? new Uint8Array(this.width * this.height),
			temperatureData: climate?.temperature ?? new Uint8Array(this.width * this.height),
		};

		return this.cachedTerrainData;
	}

	// ── River generation ─────────────────────────────────────────────
	// Biome boundaries form natural Voronoi-like edges. A smooth
	// distance field from those edges creates a potential surface:
	// cost ∝ 1 + edgeDist², so A* paths hug boundaries via continuous
	// gradients instead of binary snapping. Cardinal-only A* ensures
	// cell connectivity for subworld generation. GPU LINEAR filtering
	// smooths the visual result.

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

		// ── 4. River sources: every biome edge, no caps ───────────
		// All land cells near a biome edge are candidates. Spacing
		// prevents overlapping sources; otherwise no artificial limit.
		const candidates: Array<{idx: number; wd: number}> = [];
		for (let i = 0; i < n; i++) {
			if (height[i] > seaLevel8 && edgeDist[i] <= 2 && waterDist[i] > 4) {
				candidates.push({idx: i, wd: waterDist[i]});
			}
		}

		candidates.sort((a, b) => b.wd - a.wd);

		const minSpacing = 12;
		const taken = new Uint8Array(n);

		const markTaken = (idx: number, radius: number) => {
			const sx = idx % w;
			const sy = Math.floor(idx / w);
			for (let dy = -radius; dy <= radius; dy++) {
				for (let dx = -radius; dx <= radius; dx++) {
					if (dx * dx + dy * dy > radius * radius) {
						continue;
					}

					taken[((sy + dy + h) % h) * w + ((sx + dx + w) % w)] = 1;
				}
			}
		};

		const sources: number[] = [];
		for (const c of candidates) {
			if (taken[c.idx]) {
				continue;
			}

			sources.push(c.idx);
			markTaken(c.idx, minSpacing);
		}

		// ── 5. Trace → stamp ───────────────────────────────────────
		const riverMask = new Uint8Array(n);
		for (const src of sources) {
			const raw = this.traceToWater(src, edgeDist, waterDist, height, riverMask, seaLevel8, w, h);
			if (!raw || raw.length < 15) {
				continue;
			}

			this.stampPath(raw, height, seaLevel8, riverMask, w, h, waterDist);
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

		this.riverMask = riverMask;
		this.uploadRiverTexture(riverMask);
	}

	/**
	 * A* from source to nearest water (or existing river).
	 * Cost = 1 + edgeDist² + height/32 — the height term makes rivers
	 * follow terrain valleys, breaking straight runs naturally.
	 */
	private traceToWater(
		source: number, edgeDist: Uint16Array, waterDist: Uint16Array,
		height: Uint8Array, riverMask: Uint8Array, seaLevel8: number,
		w: number, h: number,
	): Array<[number, number]> | undefined {
		// Cardinal-only: rivers must never move diagonally (subworld needs
		// shared cell edges, not corner-only adjacency).
		const dirs: ReadonlyArray<readonly [number, number]> = [
			[-1, 0],
			[1, 0],
			[0, -1],
			[0, 1],
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
					const cost = 1 + ed * ed + (height[ni] >> 5);
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
	 * Stamp cardinal path with variable width.
	 * Width is driven by proximity to existing water — rivers widen
	 * naturally near seas/lakes without any hardcoded threshold.
	 */
	private stampPath(
		path: Array<[number, number]>,
		height: Uint8Array, seaLevel8: number,
		output: Uint8Array, w: number, h: number,
		waterDist: Uint16Array,
	) {
		for (const [px, py] of path) {
			const wd = waterDist[py * w + px];
			// Thin (1 cell) far from water, 3 cells near water bodies
			const radius = wd < 4 ? 1 : 0;
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
		const terrain = this.getTerrainData();
		if (!terrain) {
			return;
		}

		const seaLevel8 = Math.floor(this.params.seaLevel * 255);
		let relocated = false;
		for (const city of this.cities) {
			const px = Math.floor(city.x * this.width) % this.width;
			const py = Math.floor(city.y * this.height) % this.height;
			if (terrain.heightData[py * this.width + px] > seaLevel8) {
				continue; // Already on land
			}

			const land = this.findNearestLand(terrain.heightData, seaLevel8, px, py);
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
			this.cachedTerrainData = undefined;
			this.generateLayer2();
			this.generateLayer3();
			this.generateRivers();
			this.generateLayer4();
			this.generateLayer5();
		}
	}

	private findNearestLand(heightData: Uint8Array, seaLevel8: number, cx: number, cy: number): {x: number; y: number} | undefined {
		const maxR = 200;
		for (let r = 1; r <= maxR; r++) {
			for (let dx = -r; dx <= r; dx++) {
				for (const dy of [-r, r]) {
					const nx = ((cx + dx) % this.width + this.width) % this.width;
					const ny = ((cy + dy) % this.height + this.height) % this.height;
					if (heightData[ny * this.width + nx] > seaLevel8) {
						return {x: nx, y: ny};
					}
				}
			}

			for (let dy = -r + 1; dy < r; dy++) {
				for (const dx of [-r, r]) {
					const nx = ((cx + dx) % this.width + this.width) % this.width;
					const ny = ((cy + dy) % this.height + this.height) % this.height;
					if (heightData[ny * this.width + nx] > seaLevel8) {
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

	/** Read master terrain and return per-cell moisture + temperature (0-255). */
	getMasterClimateData(): {moisture: Uint8Array; temperature: Uint8Array} | null {
		const {gl} = this;
		if (!this.masterFB) {
			return null;
		}

		const n = this.width * this.height;
		gl.bindFramebuffer(gl.FRAMEBUFFER, this.masterFB.framebuffer);
		const pixels = new Uint8Array(n * 4);
		gl.readPixels(0, 0, this.width, this.height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
		gl.bindFramebuffer(gl.FRAMEBUFFER, null);

		const moisture = new Uint8Array(n);
		const temperature = new Uint8Array(n);
		for (let i = 0; i < n; i++) {
			moisture[i] = pixels[i * 4 + 1]; // G channel
			temperature[i] = pixels[i * 4 + 2]; // B channel
		}

		return {moisture, temperature};
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

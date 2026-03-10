/* eslint-disable @typescript-eslint/no-restricted-types */
import {createProgram, createQuadBuffer} from '../webgl/webgl-context';
import {CharacterRenderer} from '../character/renderer';
import {getAtlas} from '../character/atlas-loader';
import {xorshift32} from './rng';

// ── Pass 1: Map + hover highlight ──
const mapVert = `#version 300 es
in vec2 a_position;
out vec2 v_uv;
void main() {
	v_uv = a_position * 0.5 + 0.5;
	gl_Position = vec4(a_position, 0.0, 1.0);
}
`;

const mapFrag = `#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_mapTexture;
uniform sampler2D u_terrainAtlas;
uniform vec2 u_cameraPos;
uniform vec2 u_viewSize;
uniform vec2 u_hoverPos;
uniform float u_tileSize;
uniform vec2 u_canvasSize;
uniform vec2 u_mapSize;
uniform float u_terrainCount;
uniform float u_nightDarken;

void main() {
	vec2 uv = v_uv;
	vec2 mapUV = fract(u_cameraPos + (uv - 0.5) * u_viewSize);
	// Snap to pixel centers to avoid sub-pixel seams between tiles
	vec2 pixelCoord = mapUV * u_mapSize;
	pixelCoord = floor(pixelCoord) + 0.5;
	vec2 snappedUV = pixelCoord / u_mapSize;
	vec4 mapSample = texture(u_mapTexture, snappedUV);
	vec3 color = mapSample.rgb;

	// Terrain texture blending
	if (u_terrainCount > 0.0) {
		float biomeAlpha = mapSample.a;
		int idx = int(biomeAlpha * u_terrainCount + 0.5);
		idx = clamp(idx, 0, int(u_terrainCount) - 1);
		// Snap to texel centers within the 64px terrain cell for pixel-perfect sampling
		vec2 tileTexel = fract(mapUV * u_mapSize) * 64.0;
		tileTexel = floor(tileTexel) + 0.5;
		vec2 tileUV = tileTexel / 64.0;
		float atlasX = (float(idx) + tileUV.x) / u_terrainCount;
		vec3 terrainTex = texture(u_terrainAtlas, vec2(atlasX, tileUV.y)).rgb;
		color *= mix(vec3(1.0), terrainTex, 0.35);
	}

	if (u_hoverPos.x >= 0.0) {
		vec2 d = u_hoverPos - u_cameraPos;
		d.x += float(d.x > 0.5) * -1.0 + float(d.x < -0.5) * 1.0;
		d.y += float(d.y > 0.5) * -1.0 + float(d.y < -0.5) * 1.0;
		vec2 hUV = d / u_viewSize + 0.5;
		vec2 hDist = abs(uv - hUV) * u_canvasSize;
		float ht = u_tileSize * 0.5;
		if (hDist.x < ht && hDist.y < ht) {
			float edge = ht * 0.84;
			if (hDist.x > edge || hDist.y > edge)
				color = mix(color, vec3(1.0), 0.6);
			else
				color = mix(color, vec3(1.0), 0.12);
		}
	}

	// Night darkening
	if (u_nightDarken > 0.0) {
		vec3 nightTint = vec3(0.05, 0.05, 0.15);
		color = mix(color, nightTint, u_nightDarken * 0.82);
	}

	fragColor = vec4(color, 1.0);
}
`;

// ── Pass 2: Instanced sprites ──
const spriteVert = `#version 300 es
in vec2 a_quad;
in vec4 a_inst; // xy = normalized map pos, z = sprite index, w = scale

uniform vec2 u_cameraPos;
uniform vec2 u_viewSize;
uniform vec2 u_canvasSize;
uniform float u_tileSize;

out vec2 v_spriteUV;
flat out float v_spriteIdx;
out vec2 v_worldPos;

void main() {
	vec2 d = a_inst.xy - u_cameraPos;
	d.x += float(d.x > 0.5) * -1.0 + float(d.x < -0.5) * 1.0;
	d.y += float(d.y > 0.5) * -1.0 + float(d.y < -0.5) * 1.0;
	vec2 screenUV = d / u_viewSize + 0.5;

	float pxSize = u_tileSize * a_inst.w;
	vec2 offset = a_quad * pxSize / u_canvasSize;
	vec2 ndc = (screenUV + offset) * 2.0 - 1.0;

	gl_Position = vec4(ndc, 0.0, 1.0);
	v_spriteUV = a_quad + 0.5;
	v_spriteIdx = a_inst.z;
	v_worldPos = a_inst.xy;
}
`;

const spriteFrag = `#version 300 es
precision highp float;
in vec2 v_spriteUV;
flat in float v_spriteIdx;
in vec2 v_worldPos;
out vec4 fragColor;

uniform sampler2D u_atlas;
uniform float u_spriteCount;
uniform float u_nightDarken;
uniform float u_worldSeed;

float hash(float n) { return fract(sin(n) * 43758.5453123); }

vec3 getTreeColor(int palette, int type, float v) {
	if (palette == 0) { // oak
		if (type == 0) return v < 0.33 ? vec3(101,67,33)/255.0 : v < 0.66 ? vec3(92,64,51)/255.0 : vec3(79,56,41)/255.0;
		float t = v * 4.0;
		return t < 1.0 ? vec3(34,139,34)/255.0 : t < 2.0 ? vec3(50,205,50)/255.0 : t < 3.0 ? vec3(107,142,35)/255.0 : vec3(85,107,47)/255.0;
	} else if (palette == 1) { // cherry
		if (type == 0) return v < 0.5 ? vec3(60,40,30)/255.0 : vec3(80,50,40)/255.0;
		float t = v * 4.0;
		return t < 1.0 ? vec3(255,182,193)/255.0 : t < 2.0 ? vec3(255,192,203)/255.0 : t < 3.0 ? vec3(255,105,180)/255.0 : vec3(219,112,147)/255.0;
	} else if (palette == 2) { // birch
		if (type == 0) {
			float t = v * 3.0;
			return t < 1.0 ? vec3(245,245,245)/255.0 : t < 2.0 ? vec3(220,220,220)/255.0 : vec3(200,200,200)/255.0;
		}
		float t = v * 3.0;
		return t < 1.0 ? vec3(144,238,144)/255.0 : t < 2.0 ? vec3(152,251,152)/255.0 : vec3(173,255,47)/255.0;
	} else if (palette == 3) { // autumn
		if (type == 0) return v < 0.5 ? vec3(70,50,40)/255.0 : vec3(90,60,45)/255.0;
		float t = v * 5.0;
		return t < 1.0 ? vec3(255,140,0)/255.0 : t < 2.0 ? vec3(255,69,0)/255.0 : t < 3.0 ? vec3(255,215,0)/255.0 : t < 4.0 ? vec3(178,34,34)/255.0 : vec3(210,105,30)/255.0;
	} else if (palette == 4) { // pine
		if (type == 0) return v < 0.5 ? vec3(90,60,40)/255.0 : vec3(100,70,50)/255.0;
		float t = v * 4.0;
		return t < 1.0 ? vec3(0,100,0)/255.0 : t < 2.0 ? vec3(34,139,34)/255.0 : t < 3.0 ? vec3(0,128,0)/255.0 : vec3(25,80,25)/255.0;
	}
	// willow
	if (type == 0) return v < 0.5 ? vec3(101,67,33)/255.0 : vec3(92,64,51)/255.0;
	float t = v * 4.0;
	return t < 1.0 ? vec3(154,205,50)/255.0 : t < 2.0 ? vec3(173,255,47)/255.0 : t < 3.0 ? vec3(124,252,0)/255.0 : vec3(144,238,144)/255.0;
}

vec4 genTree() {
	float tx = v_worldPos.x * 1024.0;
	float ty = v_worldPos.y * 1024.0;
	float seed = u_worldSeed + tx * 1024.0 + ty;
	int pal = int(mod(hash(u_worldSeed + floor(tx/128.0) * 10000.0 + floor(ty/128.0)) * 6.0, 6.0));
	vec2 uv = vec2(v_spriteUV.x, 1.0 - v_spriteUV.y);
	vec3 col = vec3(0); float a = 0.0;
	
	// Trunk parameters (adjusted for 1:1 aspect)
	float h = 0.5 + hash(seed + 1.0) * 0.15;
	float w = 0.08 + hash(seed + 2.0) * 0.04;
	float top = 1.0 - h;
	
	// Draw trunk with noise
	if (uv.y > top && uv.y < 0.98) {
		float p = (uv.y - top) / h;
		float sway = sin(p * 6.28318) * w * 0.3;
		float noise = (hash(seed + p * 100.0) - 0.5) * w * 0.2;
		float cx = 0.5 + sway + noise;
		float tw = w * (1.0 - p * 0.7) * 0.5;
		if (abs(uv.x - cx) < tw) {
			col = getTreeColor(pal, 0, hash(seed + 10.0));
			a = 1.0;
		}
	}
	
	// Draw branches with segments
	vec3 bc = getTreeColor(pal, 0, hash(seed + 11.0));
	float branchStart = 0.3 + hash(seed + 5.0) * 0.15;
	for (int i = 0; i < 10; i++) {
		float bs = seed + float(i) * 100.0;
		float progress = branchStart + (1.0 - branchStart) * hash(bs + 1.0);
		
		// Branch probability (more in middle)
		float prob = progress < 0.7 ? 0.6 : 0.3;
		if (hash(bs + 2.0) > prob) continue;
		
		float by = top + h * progress;
		float bsway = sin(progress * 6.28318) * w * 0.3;
		float bx = 0.5 + bsway;
		
		// Angle from -60 to +60 degrees (both left and right)
		float ang = (hash(bs + 3.0) - 0.5) * 2.094;
		float len = h * (0.2 + hash(bs + 4.0) * 0.15) * (1.2 - progress);
		
		// Draw branch segments (5-8 segments)
		int segs = 5 + int(hash(bs + 6.0) * 3.0);
		float segLen = len / float(segs);
		float curAng = ang;
		vec2 curPos = vec2(bx, by);
		
		for (int j = 0; j < 8; j++) {
			if (j >= segs) break;
			float angVar = (hash(bs + float(j) * 10.0) - 0.5) * 0.26;
			curAng += angVar;
			vec2 nextPos = curPos + vec2(segLen * cos(curAng), -segLen * abs(sin(curAng)));
			
			// Draw branch segment
			vec2 bd = nextPos - curPos;
			float d = length(uv - curPos - bd * clamp(dot(uv - curPos, bd) / dot(bd, bd), 0.0, 1.0));
			if (d < 0.008 && a < 0.9) { col = bc; a = 0.9; }
			
			curPos = nextPos;
		}
		
		// Leaf cluster at branch tip (3-6 bigger circles)
		int leafCount = 3 + int(hash(bs + 20.0) * 3.0);
		for (int k = 0; k < 6; k++) {
			if (k >= leafCount) break;
			float lseed = bs + float(k) * 5.0;
			vec2 offset = vec2((hash(lseed) - 0.5) * 0.05, (hash(lseed + 1.0) - 0.5) * 0.05);
			float leafSize = 0.03 + hash(lseed + 2.0) * 0.03;
			float d = length(uv - curPos - offset);
			if (d < leafSize) {
				vec3 lc = getTreeColor(pal, 1, hash(lseed + 3.0));
				float la = 1.0 - d / leafSize;
				col = a > 0.0 ? mix(col, lc, la * 0.9) : lc;
				a = max(a, la * 0.9);
			}
		}
	}
	
	// Canopy leaves at trunk top (15-20 clusters with bigger circles)
	int canopyCount = 15 + int(hash(seed + 200.0) * 5.0);
	for (int i = 0; i < 20; i++) {
		if (i >= canopyCount) break;
		float ls = seed + float(i) * 50.0;
		float ang = hash(ls) * 6.28318;
		float dist = w + hash(ls + 1.0) * w * 2.5;
		vec2 center = vec2(0.5 + dist * cos(ang), top + dist * sin(ang) * 0.8);
		
		// Draw 2-4 bigger circles per cluster
		int clusterSize = 2 + int(hash(ls + 2.0) * 2.0);
		for (int j = 0; j < 4; j++) {
			if (j >= clusterSize) break;
			float cseed = ls + float(j) * 3.0;
			vec2 offset = vec2((hash(cseed) - 0.5) * 0.04, (hash(cseed + 1.0) - 0.5) * 0.04);
			float leafSize = 0.035 + hash(cseed + 2.0) * 0.04;
			float d = length(uv - center - offset);
			if (d < leafSize) {
				vec3 lc = getTreeColor(pal, 1, hash(cseed + 3.0));
				float la = 1.0 - d / leafSize;
				col = a > 0.0 ? mix(col, lc, la * 0.9) : lc;
				a = max(a, la * 0.9);
			}
		}
	}
	
	return vec4(col, a);
}

void main() {
	float idx = floor(v_spriteIdx);
	vec4 c;
	if (idx == 5.0) {
		c = genTree();
	} else {
		// Inset sprite UV by half a texel (each cell is 128px) to avoid atlas bleeding
		float halfTexel = 0.5 / 128.0;
		vec2 suv = clamp(v_spriteUV, halfTexel, 1.0 - halfTexel);
		vec2 atlasUV = vec2((idx + suv.x) / u_spriteCount, 1.0 - suv.y);
		c = texture(u_atlas, atlasUV);
	}
	if (c.a < 0.1) discard;
	vec3 color = c.rgb;
	if (u_nightDarken > 0.0) {
		vec3 nightTint = vec3(0.05, 0.05, 0.15);
		color = mix(color, nightTint, u_nightDarken * 0.82);
	}
	fragColor = vec4(color, c.a);
}
`;

// ── Helpers ──
async function loadImage(source: string): Promise<HTMLImageElement | HTMLCanvasElement> {
	return new Promise(resolve => {
		const img = new Image();
		img.addEventListener('load', () => {
			resolve(img);
		});

		img.addEventListener('error', () => {
			console.warn(`Failed to load sprite: ${source}. Using fallback.`);
			const c = document.createElement('canvas');
			c.width = 64;
			c.height = 64;
			const ctx = c.getContext('2d')!;
			ctx.fillStyle = '#ff00ff'; // Magenta for missing texture
			ctx.fillRect(0, 0, 64, 64);
			ctx.fillStyle = '#000000';
			ctx.font = '10px sans-serif';
			ctx.fillText('MISSING', 4, 32);
			resolve(c);
		});
		img.src = source;
	});
}

// ── Types ──
export type EntityData = {
	x: number;
	y: number;
	type: number; // Sprite atlas index
	active: boolean;
	scale?: number;
};

// Sprite atlas indices (must match load order in SPRITE_PATHS)
export const SPRITE_CITY = 0;
export const SPRITE_TREE = 5;
const SPRITE_PATHS = [
	'/assets/sprites/city.png',
	'/assets/sprites/peasant.png',
	'/assets/sprites/corovan.png',
	'/assets/sprites/player.png',
	'/assets/sprites/witch.png',
	'/assets/sprites/tree.png',
];
const SPRITE_CELL = 128;

// Terrain texture paths (indices match biome indices in shaders.ts getBiomeIndex)
// Index 6 (jungle) is generated procedurally — no PNG file needed
const TERRAIN_PATHS: Array<string | null> = [
	'/assets/sprites/water.png',
	'/assets/sprites/sand.png',
	'/assets/sprites/grass.png',
	'/assets/sprites/dirt.png',
	'/assets/sprites/mount.png',
	'/assets/sprites/snow.png',
	null, // Jungle — generated
	'/assets/sprites/swamp.png',
	'/assets/sprites/tundra.png',
];
const TERRAIN_CELL = 64;

function generateNoiseTile(
	size: number,
	baseR: number,
	baseG: number,
	baseB: number,
	variance: number,
	seed: number,
): HTMLCanvasElement {
	const c = document.createElement('canvas');
	c.width = size;
	c.height = size;
	const ctx = c.getContext('2d')!;
	const img = ctx.createImageData(size, size);
	const rng = xorshift32(seed);

	for (let i = 0; i < size * size; i++) {
		const v = (rng() - 0.5) * variance;
		img.data[i * 4] = Math.max(0, Math.min(255, baseR + v));
		img.data[i * 4 + 1] = Math.max(0, Math.min(255, baseG + v));
		img.data[i * 4 + 2] = Math.max(0, Math.min(255, baseB + v));
		img.data[i * 4 + 3] = 255;
	}

	ctx.putImageData(img, 0, 0);
	return c;
}

// ── GameRenderer ──
export class GameRenderer {
	private readonly mapProgram: WebGLProgram | null;
	private readonly spriteProgram: WebGLProgram | null;
	private readonly quadBuffer: WebGLBuffer | null;
	private readonly spriteQuadBuffer: WebGLBuffer | undefined;
	private readonly instanceBuffer: WebGLBuffer | undefined;
	private mapWidth: number; // Убрали readonly и вынесли из конструктора
	private mapHeight: number;
	private atlasTexture: WebGLTexture | undefined;
	private terrainAtlasTexture: WebGLTexture | undefined;
	private terrainCount = 0;
	private instanceCount = 0;
	private spriteDebugLogged = false;
	private nightDarken = 0;
	private worldSeed = 0;
	private characterRenderer: CharacterRenderer | undefined;

	cameraX = 0.5;
	cameraY = 0.5;
	private zoom = 32;

	constructor(
		private readonly gl: WebGL2RenderingContext,
		mapWidth: number,
		mapHeight: number,
	) {
		this.mapWidth = mapWidth;
		this.mapHeight = mapHeight;
		this.mapProgram = createProgram(gl, mapVert, mapFrag);
		this.spriteProgram = createProgram(gl, spriteVert, spriteFrag);
		this.quadBuffer = createQuadBuffer(gl);
		this.spriteQuadBuffer = this.createSpriteQuad();
		this.instanceBuffer = gl.createBuffer() ?? undefined;
	}

	private createSpriteQuad(): WebGLBuffer | undefined {
		const {gl} = this;
		const buffer = gl.createBuffer();
		if (!buffer) {
			return undefined;
		}

		// Unit quad centered at origin: [-0.5,+0.5], triangle strip
		gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
			-0.5,
			-0.5,
			0.5,
			-0.5,
			-0.5,
			0.5,
			0.5,
			0.5,
		]), gl.STATIC_DRAW);
		return buffer;
	}

	async loadSprites(): Promise<void> {
		const {gl} = this;
		const images = await Promise.all(SPRITE_PATHS.map(async p => loadImage(p)));
		const count = images.length;
		const atlasW = count * SPRITE_CELL;
		const atlasH = SPRITE_CELL;

		const offscreen = document.createElement('canvas');
		offscreen.width = atlasW;
		offscreen.height = atlasH;
		const ctx = offscreen.getContext('2d')!;

		for (let i = 0; i < count; i++) {
			const img = images[i];
			const aspect = img.width / img.height;
			let drawW = SPRITE_CELL;
			let drawH = SPRITE_CELL;
			if (aspect > 1) {
				drawH = SPRITE_CELL / aspect;
			} else {
				drawW = SPRITE_CELL * aspect;
			}

			const offsetX = (SPRITE_CELL - drawW) / 2;
			const offsetY = SPRITE_CELL - drawH; // Align bottom
			ctx.drawImage(img, i * SPRITE_CELL + offsetX, offsetY, drawW, drawH);
		}

		const tex = gl.createTexture();
		if (!tex) {
			return;
		}

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, offscreen);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		this.atlasTexture = tex;
	}

	async loadTerrainTextures(): Promise<void> {
		const {gl} = this;
		const sources: Array<HTMLCanvasElement | HTMLImageElement> = await Promise.all(TERRAIN_PATHS.map(async p => {
			if (p === null) {
				// Generate jungle noise tile: dark green with variance
				return generateNoiseTile(TERRAIN_CELL, 40, 90, 35, 60, 42);
			}

			return loadImage(p);
		}));
		const count = sources.length;
		const atlasW = count * TERRAIN_CELL;
		const atlasH = TERRAIN_CELL;

		const offscreen = document.createElement('canvas');
		offscreen.width = atlasW;
		offscreen.height = atlasH;
		const ctx = offscreen.getContext('2d')!;
		ctx.imageSmoothingEnabled = false; // Pixel-perfect upscale for pixel-art

		for (let i = 0; i < count; i++) {
			ctx.drawImage(sources[i], i * TERRAIN_CELL, 0, TERRAIN_CELL, TERRAIN_CELL);
		}

		const tex = gl.createTexture();
		if (!tex) {
			return;
		}

		gl.activeTexture(gl.TEXTURE1);
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, offscreen);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		this.terrainAtlasTexture = tex;
		this.terrainCount = count;
	}

	initCharacterRenderer(): void {
		this.characterRenderer = new CharacterRenderer(this.gl);
		const atlas = getAtlas();
		if (atlas) {
			this.characterRenderer.uploadAtlas(atlas);
		}
	}

	getCharacterRenderer(): CharacterRenderer | undefined {
		return this.characterRenderer;
	}

	setNightDarken(factor: number): void {
		this.nightDarken = Math.max(0, Math.min(1, factor));
		this.characterRenderer?.setNightDarken(this.nightDarken);
	}

	setWorldSeed(seed: number): void {
		this.worldSeed = seed;
	}

	uploadEntities(entities: EntityData[]): void {
		const {gl} = this;
		if (!this.instanceBuffer) {
			return;
		}

		const count = Math.min(entities.length, 8192);
		const data = new Float32Array(count * 4);
		for (let i = 0; i < count; i++) {
			const ent = entities[i];
			const idx = i * 4;
			data[idx] = (ent.x + 0.5) / this.mapWidth;
			data[idx + 1] = (ent.y + 0.5) / this.mapHeight;
			data[idx + 2] = ent.type;
			data[idx + 3] = ent.scale ?? 1;
		}

		gl.bindBuffer(gl.ARRAY_BUFFER, this.instanceBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
		this.instanceCount = count;
	}

	setCamera(x: number, y: number): void {
		this.cameraX = x;
		this.cameraY = y;
	}

	setZoom(tilesVisible: number): void {
		this.zoom = tilesVisible;
	}

	getZoom(): number {
		return this.zoom;
	}

	getMapWidth(): number {
		return this.mapWidth;
	}

	getMapHeight(): number {
		return this.mapHeight;
	}

	/**
	 * Convert world tile coordinates to screen pixel coordinates.
	 * Accepts fractional positions for smooth movement.
	 * Returns null if off-screen.
	 */
	worldToScreen(
		worldX: number, worldY: number,
		canvasWidth: number, canvasHeight: number,
	): {sx: number; sy: number} | null {
		const aspect = canvasWidth / canvasHeight;
		const viewW = this.zoom / this.mapWidth;
		const viewH = (this.zoom / aspect) / this.mapHeight;

		// Convert to normalized map UV (center of tile = +0.5)
		let dx = (worldX + 0.5) / this.mapWidth - this.cameraX;
		let dy = (worldY + 0.5) / this.mapHeight - this.cameraY;

		// Torus wrapping
		if (dx > 0.5) {
			dx -= 1;
		} else if (dx < -0.5) {
			dx += 1;
		}

		if (dy > 0.5) {
			dy -= 1;
		} else if (dy < -0.5) {
			dy += 1;
		}

		// ScreenUV is in GL UV space: Y=0 is bottom, Y=1 is top
		const screenUVx = dx / viewW + 0.5;
		const screenUVy = dy / viewH + 0.5;

		// Cull off-screen
		if (screenUVx < -0.1 || screenUVx > 1.1 || screenUVy < -0.1 || screenUVy > 1.1) {
			return null;
		}

		return {
			sx: screenUVx * canvasWidth,
			// Flip Y: GL UV Y=0 is screen bottom, but canvas Y=0 is screen top
			sy: (1 - screenUVy) * canvasHeight,
		};
	}

	screenToTile(
		screenX: number,
		screenY: number,
		canvasWidth: number,
		canvasHeight: number,
	): {x: number; y: number} {
		const uvX = screenX / canvasWidth;
		const uvY = screenY / canvasHeight;

		const aspect = canvasWidth / canvasHeight;
		const viewW = this.zoom / this.mapWidth;
		const viewH = (this.zoom / aspect) / this.mapHeight;

		let mapUvX = this.cameraX + (uvX - 0.5) * viewW;
		let mapUvY = this.cameraY + (0.5 - uvY) * viewH;
		mapUvX = ((mapUvX % 1) + 1) % 1;
		mapUvY = ((mapUvY % 1) + 1) % 1;

		return {
			x: Math.floor(mapUvX * this.mapWidth),
			y: Math.floor(mapUvY * this.mapHeight),
		};
	}

	render(
		mapTexture: WebGLTexture,
		_playerX: number,
		_playerY: number,
		canvasWidth: number,
		canvasHeight: number,
		hoverTileX = -1,
		hoverTileY = -1,
	): void {
		const {gl} = this;

		gl.bindFramebuffer(gl.FRAMEBUFFER, null);
		gl.viewport(0, 0, canvasWidth, canvasHeight);
		gl.clearColor(0, 0, 0, 1);
		gl.clear(gl.COLOR_BUFFER_BIT);

		// ── Pass 1: Map ──
		this.renderMap(mapTexture, canvasWidth, canvasHeight, hoverTileX, hoverTileY);

		// ── Pass 2: Sprites ──
		this.renderSprites(canvasWidth, canvasHeight);
	}

	private renderMap(
		mapTexture: WebGLTexture,
		canvasWidth: number,
		canvasHeight: number,
		hoverTileX: number,
		hoverTileY: number,
	): void {
		const {gl} = this;
		if (!this.mapProgram || !this.quadBuffer) {
			return;
		}

		gl.useProgram(this.mapProgram);

		const aspect = canvasWidth / canvasHeight;
		const viewW = this.zoom / this.mapWidth;
		const viewH = (this.zoom / aspect) / this.mapHeight;
		const tileSize = canvasWidth / this.zoom;

		gl.uniform2f(gl.getUniformLocation(this.mapProgram, 'u_cameraPos'), this.cameraX, this.cameraY);
		gl.uniform2f(gl.getUniformLocation(this.mapProgram, 'u_viewSize'), viewW, viewH);
		const hx = hoverTileX >= 0 ? (hoverTileX + 0.5) / this.mapWidth : -1;
		const hy = hoverTileY >= 0 ? (hoverTileY + 0.5) / this.mapHeight : -1;
		gl.uniform2f(gl.getUniformLocation(this.mapProgram, 'u_hoverPos'), hx, hy);
		gl.uniform1f(gl.getUniformLocation(this.mapProgram, 'u_tileSize'), tileSize);
		gl.uniform2f(gl.getUniformLocation(this.mapProgram, 'u_canvasSize'), canvasWidth, canvasHeight);
		gl.uniform2f(gl.getUniformLocation(this.mapProgram, 'u_mapSize'), this.mapWidth, this.mapHeight);
		gl.uniform1f(gl.getUniformLocation(this.mapProgram, 'u_terrainCount'), this.terrainCount);
		gl.uniform1f(gl.getUniformLocation(this.mapProgram, 'u_nightDarken'), this.nightDarken);

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, mapTexture);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
		gl.uniform1i(gl.getUniformLocation(this.mapProgram, 'u_mapTexture'), 0);

		if (this.terrainAtlasTexture) {
			gl.activeTexture(gl.TEXTURE1);
			gl.bindTexture(gl.TEXTURE_2D, this.terrainAtlasTexture);
			gl.uniform1i(gl.getUniformLocation(this.mapProgram, 'u_terrainAtlas'), 1);
		}

		const posLoc = gl.getAttribLocation(this.mapProgram, 'a_position');
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
	}

	private renderSprites(canvasWidth: number, canvasHeight: number): void {
		const {gl} = this;
		if (
			!this.spriteProgram
			|| !this.spriteQuadBuffer
			|| !this.instanceBuffer
			|| !this.atlasTexture
			|| this.instanceCount === 0
		) {
			if (!this.spriteDebugLogged) {
				console.warn('[Sprites] early return:', {
					program: Boolean(this.spriteProgram),
					quadBuf: Boolean(this.spriteQuadBuffer),
					instBuf: Boolean(this.instanceBuffer),
					atlas: Boolean(this.atlasTexture),
					count: this.instanceCount,
				});
				this.spriteDebugLogged = true;
			}

			return;
		}

		if (!this.spriteDebugLogged) {
			const quadLoc = gl.getAttribLocation(this.spriteProgram, 'a_quad');
			const instLoc = gl.getAttribLocation(this.spriteProgram, 'a_inst');
			console.log('[Sprites] rendering', {
				instances: this.instanceCount,
				canvas: [canvasWidth, canvasHeight],
				quadLoc,
				instLoc,
				camera: [this.cameraX, this.cameraY],
				zoom: this.zoom,
			});
			this.spriteDebugLogged = true;
		}

		// Reset vertex attribute state from previous pass to avoid interference
		for (let i = 0; i < 8; i++) {
			gl.disableVertexAttribArray(i);
			gl.vertexAttribDivisor(i, 0);
		}

		gl.enable(gl.BLEND);
		gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
		gl.useProgram(this.spriteProgram);

		const aspect = canvasWidth / canvasHeight;
		const viewW = this.zoom / this.mapWidth;
		const viewH = (this.zoom / aspect) / this.mapHeight;
		const tileSize = canvasWidth / this.zoom;

		gl.uniform2f(gl.getUniformLocation(this.spriteProgram, 'u_cameraPos'), this.cameraX, this.cameraY);
		gl.uniform2f(gl.getUniformLocation(this.spriteProgram, 'u_viewSize'), viewW, viewH);
		gl.uniform2f(gl.getUniformLocation(this.spriteProgram, 'u_canvasSize'), canvasWidth, canvasHeight);
		gl.uniform1f(gl.getUniformLocation(this.spriteProgram, 'u_tileSize'), tileSize);
		gl.uniform1f(gl.getUniformLocation(this.spriteProgram, 'u_spriteCount'), SPRITE_PATHS.length);
		gl.uniform1f(gl.getUniformLocation(this.spriteProgram, 'u_nightDarken'), this.nightDarken);
		gl.uniform1f(gl.getUniformLocation(this.spriteProgram, 'u_worldSeed'), this.worldSeed);

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.atlasTexture);
		gl.uniform1i(gl.getUniformLocation(this.spriteProgram, 'u_atlas'), 0);

		// Quad vertices (per-vertex)
		const quadLoc = gl.getAttribLocation(this.spriteProgram, 'a_quad');
		gl.bindBuffer(gl.ARRAY_BUFFER, this.spriteQuadBuffer);
		gl.enableVertexAttribArray(quadLoc);
		gl.vertexAttribPointer(quadLoc, 2, gl.FLOAT, false, 0, 0);
		gl.vertexAttribDivisor(quadLoc, 0);

		// Instance data (per-instance)
		const instLoc = gl.getAttribLocation(this.spriteProgram, 'a_inst');
		gl.bindBuffer(gl.ARRAY_BUFFER, this.instanceBuffer);
		gl.enableVertexAttribArray(instLoc);
		gl.vertexAttribPointer(instLoc, 4, gl.FLOAT, false, 0, 0);
		gl.vertexAttribDivisor(instLoc, 1);

		gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, this.instanceCount);

		// Clean up divisor
		gl.vertexAttribDivisor(instLoc, 0);
		gl.disable(gl.BLEND);
	}

	updateMapDimensions(width: number, height: number): void {
		this.mapWidth = width;
		this.mapHeight = height;
	}

	destroy(): void {
		const {gl} = this;
		if (this.mapProgram) {
			gl.deleteProgram(this.mapProgram);
		}

		if (this.spriteProgram) {
			gl.deleteProgram(this.spriteProgram);
		}

		if (this.quadBuffer) {
			gl.deleteBuffer(this.quadBuffer);
		}

		if (this.spriteQuadBuffer) {
			gl.deleteBuffer(this.spriteQuadBuffer);
		}

		if (this.instanceBuffer) {
			gl.deleteBuffer(this.instanceBuffer);
		}

		if (this.atlasTexture) {
			gl.deleteTexture(this.atlasTexture);
		}

		if (this.terrainAtlasTexture) {
			gl.deleteTexture(this.terrainAtlasTexture);
		}

		this.characterRenderer?.destroy();
	}
}

/* eslint-disable @typescript-eslint/no-restricted-types */
import {createProgram, createQuadBuffer} from '../webgl/webgl-context';
import {CharacterRenderer} from '../character/renderer';
import {getAtlas} from '../character/atlas-loader';
import {TREE_MAP_GLSL} from './tree-spawner';
import {MOUNTAIN_MAP_GLSL} from './mountain-spawner';
import {ROAD_MAP_GLSL} from './road-spawner';
import {DIRT_ROAD_MAP_GLSL} from './dirt-road-spawner';
import {BIOME_TEXTURE_GLSL} from './biome-textures';

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
uniform sampler2D u_masterTexture;
uniform sampler2D u_featureMap;
uniform sampler2D u_landmarkMap;
uniform sampler2D u_landmarkAtlas;
uniform float u_landmarkAtlasCount;
uniform vec2 u_cameraPos;
uniform vec2 u_viewSize;
uniform vec2 u_hoverPos;
uniform float u_tileSize;
uniform vec2 u_canvasSize;
uniform vec2 u_mapSize;
uniform float u_nightDarken;
uniform float u_seaLevel;
uniform float u_worldSeed;
uniform float u_mtnThreshold;

// Landmark night lights — modular: any landmark with a population emits.
// u_lights[i] = (normX, normY, radiusCells, intensity).
// u_lightColors[i] = (r, g, b) emitted tint. One color per light → any new
// landmark type can introduce its own glow without engine changes.
#define MAX_LANDMARK_LIGHTS 64
uniform int u_lightCount;
uniform vec4 u_lights[MAX_LANDMARK_LIGHTS];
uniform vec3 u_lightColors[MAX_LANDMARK_LIGHTS];

${BIOME_TEXTURE_GLSL}
${ROAD_MAP_GLSL}
${DIRT_ROAD_MAP_GLSL}
${TREE_MAP_GLSL}
${MOUNTAIN_MAP_GLSL}

// === Landmark sprite draw — samples sprite atlas per landmark cell. ===
// landmarkMap encodes per cell: 0=none, 1=city, 2=village, 3=spire-active,
// 4=spire-depleted. Adding a kind = one entry below + one pixel value in
// the upload array.
void landmarkDraw(vec2 cell, vec2 worldPos, inout vec3 col) {
	vec2 cellUV = (cell + 0.5) / u_mapSize;
	float lid = floor(texture(u_landmarkMap, cellUV).r * 255.0 + 0.5);
	if (lid < 0.5) return;

	float sprIdx;
	float scale;
	if (lid < 1.5) { sprIdx = 0.0; scale = 1.8; }
	else if (lid < 2.5) { sprIdx = 6.0; scale = 1.2; }
	else if (lid < 3.5) { sprIdx = 7.0; scale = 1.6; }
	else if (lid < 4.5) { sprIdx = 8.0; scale = 1.6; }
	else return;

	vec2 center = cell + 0.5;
	vec2 diff = worldPos - center;
	if (diff.x > u_mapSize.x * 0.5) diff.x -= u_mapSize.x;
	if (diff.x < -u_mapSize.x * 0.5) diff.x += u_mapSize.x;
	if (diff.y > u_mapSize.y * 0.5) diff.y -= u_mapSize.y;
	if (diff.y < -u_mapSize.y * 0.5) diff.y += u_mapSize.y;

	vec2 sprUV = diff / scale + 0.5;
	if (sprUV.x < 0.0 || sprUV.x >= 1.0
		|| sprUV.y < 0.0 || sprUV.y >= 1.0) return;

	float halfTexel = 0.5 / 128.0;
	vec2 suv = clamp(sprUV, halfTexel, 1.0 - halfTexel);
	vec2 atlasUV = vec2(
		(sprIdx + suv.x) / u_landmarkAtlasCount, 1.0 - suv.y);
	vec4 c = texture(u_landmarkAtlas, atlasUV);
	if (c.a < 0.1) return;
	col = mix(col, c.rgb, c.a);
}

// === Per-cell painter overlay: tree + mountain + landmark, far → near. ===
// Iterates a 3×3 window around the pixel from high worldY (top of screen,
// far) down to low worldY (bottom, near). Within each cell the order is
// feature (tree, mountain) → landmark, so closer-cell features paint over
// farther-cell landmarks while landmarks remain on top of features inside
// their own cell.
void decorationOverlay(vec2 mapUV, inout vec3 color) {
	vec2 worldPos = mapUV * u_mapSize;
	vec2 cc = floor(worldPos);
	for (int dy = 1; dy >= -1; dy--) {
		for (int dx = -1; dx <= 1; dx++) {
			vec2 cell = mod(
				cc + vec2(float(dx), float(dy)), u_mapSize);

			// Tree (anchor at cell + (0.5, 1.0), 2×2 footprint).
			{
				vec2 diff = worldPos - (cell + vec2(0.5, 1.0));
				if (diff.x > u_mapSize.x * 0.5) diff.x -= u_mapSize.x;
				if (diff.x < -u_mapSize.x * 0.5) diff.x += u_mapSize.x;
				if (diff.y > u_mapSize.y * 0.5) diff.y -= u_mapSize.y;
				if (diff.y < -u_mapSize.y * 0.5) diff.y += u_mapSize.y;
				vec2 luv = (diff + 1.0) / 2.0;
				if (luv.x >= 0.0 && luv.x < 1.0
					&& luv.y >= 0.0 && luv.y < 1.0) {
					vec4 t = treeDraw(cell, luv, color);
					if (t.a > 0.5) color = t.rgb;
				}
			}

			// Mountain (centered on cell, 2×2 footprint).
			{
				vec2 diff = worldPos - (cell + 0.5);
				if (diff.x > u_mapSize.x * 0.5) diff.x -= u_mapSize.x;
				if (diff.x < -u_mapSize.x * 0.5) diff.x += u_mapSize.x;
				if (diff.y > u_mapSize.y * 0.5) diff.y -= u_mapSize.y;
				if (diff.y < -u_mapSize.y * 0.5) diff.y += u_mapSize.y;
				vec2 luv = (diff + 1.0) / 2.0;
				if (luv.x >= 0.0 && luv.x < 1.0
					&& luv.y >= 0.0 && luv.y < 1.0) {
					vec4 m = mtnDraw(cell, luv, color);
					if (m.a > 0.5) color = m.rgb;
				}
			}

			// Landmark sprite (city / village) — last within the cell.
			landmarkDraw(cell, worldPos, color);
		}
	}
}

void main() {
	vec2 uv = v_uv;
	vec2 mapUV = fract(u_cameraPos + (uv - 0.5) * u_viewSize);
	// Snap to pixel centers to avoid sub-pixel seams between tiles
	vec2 pixelCoord = mapUV * u_mapSize;
	pixelCoord = floor(pixelCoord) + 0.5;
	vec2 snappedUV = pixelCoord / u_mapSize;
	// TEST: macroworld map canvas disabled — render only procedural biome textures.
	// To revert, uncomment the two lines below and remove the vec3(1.0) line.
	// vec4 mapSample = texture(u_mapTexture, snappedUV);
	// vec3 color = mapSample.rgb;
	vec3 color = vec3(1.0);

	// Procedural biome texture overlay (neighbor-aware)
	color = biomeTextureOverlay(mapUV, color);

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

	// Road surface overlay (ground level — under hover highlight)
	if (u_tileSize > 4.0 && u_mtnThreshold > 0.0) {
		color = roadOverlay(mapUV, color);
		color = dirtRoadOverlay(mapUV, color);
	}

	// Per-cell painter overlay: tree → mountain → landmark, far → near.
	// Replaces the prior global tree/mountain passes + sprite landmark pass
	// so trees in nearer cells correctly overlap landmarks in farther cells.
	if (u_tileSize > 6.0 && u_mtnThreshold > 0.0) {
		decorationOverlay(mapUV, color);
	}

	// Night darkening
	if (u_nightDarken > 0.0) {
		vec3 nightTint = vec3(0.05, 0.05, 0.15);
		color = mix(color, nightTint, u_nightDarken * 0.82);

		// Landmark light glow — per-light color, additive contribution.
		if (u_lightCount > 0) {
			vec3 totalGlow = vec3(0.0);
			for (int i = 0; i < MAX_LANDMARK_LIGHTS; i++) {
				if (i >= u_lightCount) break;
				vec4 L = u_lights[i];
				vec2 d = mapUV - L.xy;
				d.x -= floor(d.x + 0.5);
				d.y -= floor(d.y + 0.5);
				d *= u_mapSize;
				float r = max(L.z, 0.001);
				float f = max(0.0, 1.0 - length(d) / r);
				totalGlow += u_lightColors[i] * (f * f * L.w);
			}
			totalGlow = clamp(totalGlow, 0.0, 1.5) * u_nightDarken;
			color += totalGlow * 0.6;
		}
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
// Per-sprite luminosity (0..1) — luminous sprites resist night darkening
// and glow warmly at night. Data-driven: any sprite can opt-in.
#define MAX_SPRITE_TYPES 32
uniform float u_spriteLuminous[MAX_SPRITE_TYPES];

void main() {
	float idx = floor(v_spriteIdx);
	// Inset sprite UV by half a texel (each cell is 128px) to avoid atlas bleeding
	float halfTexel = 0.5 / 128.0;
	vec2 suv = clamp(v_spriteUV, halfTexel, 1.0 - halfTexel);
	vec2 atlasUV = vec2((idx + suv.x) / u_spriteCount, 1.0 - suv.y);
	vec4 c = texture(u_atlas, atlasUV);
	if (c.a < 0.1) discard;
	vec3 color = c.rgb;
	float lum = u_spriteLuminous[int(clamp(idx, 0.0, float(MAX_SPRITE_TYPES - 1)))];
	if (u_nightDarken > 0.0) {
		vec3 nightTint = vec3(0.05, 0.05, 0.15);
		// Luminous sprites (cities, villages, …) keep their daylight colour
		// and gain a warm additive glow proportional to night intensity.
		float darkAmt = u_nightDarken * 0.82 * (1.0 - lum);
		color = mix(color, nightTint, darkAmt);
		color += vec3(1.0, 0.78, 0.42) * lum * u_nightDarken * 0.45;
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
export const SPRITE_VILLAGE = 6;
export const SPRITE_SPIRE_ACTIVE = 7;
export const SPRITE_SPIRE_DEPLETED = 8;
const SPRITE_PATHS = [
	'/assets/sprites/city.png',
	'/assets/sprites/peasant.png',
	'/assets/sprites/corovan.png',
	'/assets/sprites/player.png',
	'/assets/sprites/witch.png',
	'/assets/sprites/tree.png',
	'/assets/sprites/village_256.png',
	'/assets/sprites/spireA_256.png',
	'/assets/sprites/spireD_256.png',
];
// Per-sprite luminosity at night (0..1). Landmarks glow; everything else is dark.
// Indices match SPRITE_PATHS order. Add a new luminous sprite = one entry.
const MAX_SPRITE_TYPES = 32;
const SPRITE_LUMINOSITY = new Float32Array(MAX_SPRITE_TYPES);
SPRITE_LUMINOSITY[SPRITE_CITY] = 1;
SPRITE_LUMINOSITY[SPRITE_VILLAGE] = 0.7;
SPRITE_LUMINOSITY[SPRITE_SPIRE_ACTIVE] = 1;
SPRITE_LUMINOSITY[SPRITE_SPIRE_DEPLETED] = 0;
const SPRITE_CELL = 128;

// Max landmark lights uploaded to map shader (must match GLSL #define).
const MAX_LANDMARK_LIGHTS = 64;

/** Any macroworld landmark that emits night light. Population drives
 * radius + intensity. `color` (r, g, b ∈ 0..1) is the emitted tint —
 * defaults to warm hearth-fire if omitted. New landmark types can pass
 * any color without touching the renderer. */
export type LandmarkLight = {
	x: number;
	y: number;
	population: number;
	color?: [number, number, number];
};

/** Default warm hearth glow used when a light omits its color. */
const DEFAULT_LIGHT_COLOR: [number, number, number] = [1, 0.78, 0.42];

// ── GameRenderer ──
export class GameRenderer {
	private readonly mapProgram: WebGLProgram | null;
	private readonly spriteProgram: WebGLProgram | null;
	private readonly quadBuffer: WebGLBuffer | null;
	private readonly spriteQuadBuffer: WebGLBuffer | undefined;
	private readonly instanceBuffer: WebGLBuffer | undefined;
	private atlasTexture: WebGLTexture | undefined;
	private featureTexture: WebGLTexture | undefined;
	private landmarkMapTexture: WebGLTexture | undefined;
	private seaLevel = 0.45;
	private instanceCount = 0;
	private spriteDebugLogged = false;
	private nightDarken = 0;
	private worldSeed = 0;
	private mtnThreshold = 0;
	private characterRenderer: CharacterRenderer | undefined;
	private landmarkLights: LandmarkLight[] = [];
	private readonly lightUniformBuffer = new Float32Array(MAX_LANDMARK_LIGHTS * 4);
	private readonly lightColorBuffer = new Float32Array(MAX_LANDMARK_LIGHTS * 3);

	cameraX = 0.5;
	cameraY = 0.5;
	private zoom = 32;

	constructor(
		private readonly gl: WebGL2RenderingContext,
		private mapWidth: number,
		private mapHeight: number,
	) {
		this.mapProgram = createProgram(gl, mapVert, mapFrag);
		if (!this.mapProgram) {
			console.error('[GameRenderer] mapProgram FAILED to compile');
		}

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

	setSeaLevel(level: number): void {
		this.seaLevel = level;
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

	setMountainThreshold(threshold: number): void {
		this.mtnThreshold = threshold;
	}

	/**
	 * Register landmarks that emit light at night. Modular: any caller can
	 * push more entries (cities, villages, future towers, …). Population
	 * drives both glow radius and intensity.
	 */
	setLandmarkLights(lights: LandmarkLight[]): void {
		this.landmarkLights = lights;
	}

	/** Upload feature layer (width × height Uint8Array, FeatureType per cell). */
	uploadFeatureMap(data: Uint8Array, width: number, height: number): void {
		const {gl} = this;
		if (this.featureTexture) {
			gl.deleteTexture(this.featureTexture);
		}

		const tex = gl.createTexture();
		if (!tex) {
			return;
		}

		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, width, height, 0, gl.RED, gl.UNSIGNED_BYTE, data);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
		gl.bindTexture(gl.TEXTURE_2D, null);
		this.featureTexture = tex;
	}

	/**
	 * Upload landmark layer (width × height Uint8Array, kind per cell):
	 *   0 = none, 1 = city, 2 = village.
	 * Drawn inside the map fragment shader in painter order with trees and
	 * mountains so closer-cell features overlap farther-cell landmarks.
	 */
	uploadLandmarkMap(data: Uint8Array, width: number, height: number): void {
		const {gl} = this;
		if (this.landmarkMapTexture) {
			gl.deleteTexture(this.landmarkMapTexture);
		}

		const tex = gl.createTexture();
		if (!tex) {
			return;
		}

		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, width, height, 0, gl.RED, gl.UNSIGNED_BYTE, data);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
		gl.bindTexture(gl.TEXTURE_2D, null);
		this.landmarkMapTexture = tex;
	}

	uploadEntities(entities: EntityData[]): void {
		const {gl} = this;
		if (!this.instanceBuffer) {
			return;
		}

		const count = Math.min(entities.length, 16_384);
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
		heightTexture?: WebGLTexture,
	): void {
		const {gl} = this;

		gl.bindFramebuffer(gl.FRAMEBUFFER, null);
		gl.viewport(0, 0, canvasWidth, canvasHeight);
		gl.clearColor(0, 0, 0, 1);
		gl.clear(gl.COLOR_BUFFER_BIT);

		// ── Pass 1: Map ──
		this.renderMap(mapTexture, canvasWidth, canvasHeight, hoverTileX, hoverTileY, heightTexture);

		// ── Pass 2: Sprites ──
		this.renderSprites(canvasWidth, canvasHeight);
	}

	private renderMap(
		mapTexture: WebGLTexture,
		canvasWidth: number,
		canvasHeight: number,
		hoverTileX: number,
		hoverTileY: number,
		heightTexture?: WebGLTexture,
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
		gl.uniform1f(gl.getUniformLocation(this.mapProgram, 'u_seaLevel'), this.seaLevel);
		gl.uniform1f(gl.getUniformLocation(this.mapProgram, 'u_nightDarken'), this.nightDarken);
		gl.uniform1f(gl.getUniformLocation(this.mapProgram, 'u_worldSeed'), this.worldSeed);
		gl.uniform1f(gl.getUniformLocation(this.mapProgram, 'u_mtnThreshold'), this.mtnThreshold);

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, mapTexture);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
		gl.uniform1i(gl.getUniformLocation(this.mapProgram, 'u_mapTexture'), 0);

		if (heightTexture) {
			gl.activeTexture(gl.TEXTURE2);
			gl.bindTexture(gl.TEXTURE_2D, heightTexture);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
			gl.uniform1i(gl.getUniformLocation(this.mapProgram, 'u_masterTexture'), 2);
		}

		// Feature map (FeatureType per cell)
		if (this.featureTexture) {
			gl.activeTexture(gl.TEXTURE3);
			gl.bindTexture(gl.TEXTURE_2D, this.featureTexture);
			gl.uniform1i(gl.getUniformLocation(this.mapProgram, 'u_featureMap'), 3);
		}

		// Landmark map (city/village kind per cell) + sprite atlas
		if (this.landmarkMapTexture) {
			gl.activeTexture(gl.TEXTURE4);
			gl.bindTexture(gl.TEXTURE_2D, this.landmarkMapTexture);
			gl.uniform1i(gl.getUniformLocation(this.mapProgram, 'u_landmarkMap'), 4);
		}

		if (this.atlasTexture) {
			gl.activeTexture(gl.TEXTURE5);
			gl.bindTexture(gl.TEXTURE_2D, this.atlasTexture);
			gl.uniform1i(gl.getUniformLocation(this.mapProgram, 'u_landmarkAtlas'), 5);
			gl.uniform1f(
				gl.getUniformLocation(this.mapProgram, 'u_landmarkAtlasCount'),
				SPRITE_PATHS.length,
			);
		}

		this.uploadLandmarkLights(viewW, viewH);

		const posLoc = gl.getAttribLocation(this.mapProgram, 'a_position');
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
	}

	/**
	 * Pack visible landmark lights into the shader uniform array.
	 * Skipped during full daylight (no glow contribution anyway).
	 */
	private uploadLandmarkLights(viewW: number, viewH: number): void {
		const {gl} = this;
		if (!this.mapProgram) {
			return;
		}

		const countLoc = gl.getUniformLocation(this.mapProgram, 'u_lightCount');
		if (this.nightDarken <= 0.01 || this.landmarkLights.length === 0) {
			gl.uniform1i(countLoc, 0);
			return;
		}

		const buf = this.lightUniformBuffer;
		let count = 0;
		for (const l of this.landmarkLights) {
			if (count >= MAX_LANDMARK_LIGHTS) {
				break;
			}

			const pop = Math.max(1, l.population);
			// Population → soft log-scaled glow.
			const intensity = Math.min(1, 0.18 + Math.log10(pop) * 0.32);
			const radius = 1.5 + Math.sqrt(pop) * 0.35;
			const nx = (l.x + 0.5) / this.mapWidth;
			const ny = (l.y + 0.5) / this.mapHeight;

			let dx = nx - this.cameraX;
			if (dx > 0.5) {
				dx -= 1;
			} else if (dx < -0.5) {
				dx += 1;
			}

			let dy = ny - this.cameraY;
			if (dy > 0.5) {
				dy -= 1;
			} else if (dy < -0.5) {
				dy += 1;
			}

			const marginX = viewW * 0.5 + radius / this.mapWidth;
			const marginY = viewH * 0.5 + radius / this.mapHeight;
			if (Math.abs(dx) > marginX || Math.abs(dy) > marginY) {
				continue;
			}

			const idx = count * 4;
			buf[idx] = nx;
			buf[idx + 1] = ny;
			buf[idx + 2] = radius;
			buf[idx + 3] = intensity;

			const color = l.color ?? DEFAULT_LIGHT_COLOR;
			const cIdx = count * 3;
			this.lightColorBuffer[cIdx] = color[0];
			this.lightColorBuffer[cIdx + 1] = color[1];
			this.lightColorBuffer[cIdx + 2] = color[2];
			count++;
		}

		gl.uniform1i(countLoc, count);
		if (count > 0) {
			gl.uniform4fv(
				gl.getUniformLocation(this.mapProgram, 'u_lights[0]'),
				buf.subarray(0, count * 4),
			);
			gl.uniform3fv(
				gl.getUniformLocation(this.mapProgram, 'u_lightColors[0]'),
				this.lightColorBuffer.subarray(0, count * 3),
			);
		}
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
		gl.uniform1fv(
			gl.getUniformLocation(this.spriteProgram, 'u_spriteLuminous[0]'),
			SPRITE_LUMINOSITY,
		);

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

		if (this.featureTexture) {
			gl.deleteTexture(this.featureTexture);
		}

		if (this.landmarkMapTexture) {
			gl.deleteTexture(this.landmarkMapTexture);
		}

		this.characterRenderer?.destroy();
	}
}

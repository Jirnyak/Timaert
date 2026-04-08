// === Subworld 3D WebGL2 Renderer — Might & Magic style ===
//
// First-person renderer for the 1024×1024 subworld plane.
// Renders heightmap terrain, extruded 2D structures (boxes/cylinders),
// and billboarded sprites (trees, NPCs) using WebGL2.
//
// Architecture:
//   - Terrain: grid mesh with Y from heightmap
//   - Structures: instanced geometry (rect→box, circle→cylinder)
//   - Sprites: camera-facing billboarded quads
//   - Sky: fullscreen gradient quad
//
// All data comes from existing systems:
//   - Structure[] from map-data.ts (2D shapes + height + textures)
//   - Float32Array heightmap from base-generator.ts
//   - CameraState from camera.ts
//   - Procedural textures from textures.ts
//
// Reference: Might & Magic 6/7, Quake — simple, minimal, elegant.

import {type CameraState, HEIGHT_SCALE} from './camera';
import type {Structure} from './map-data';
import {
	getTexture, TEXTURE_SIZE, generateTreeAtlas, TREE_TYPES,
} from './textures';
import {
	mat4Perspective, mat4LookAt, mat4Multiply,
	type Mat4, type Vec3,
} from './math3d';

// ── Constants ───────────────────────────────────────────────────

/** Terrain mesh resolution (vertices per axis). Lower than composite for VRAM savings; GPU bilinear sampling smooths heightmap. */
const TERRAIN_RES = 1024;

/** Texture atlas grid: NxN tiles of TEXTURE_SIZE×TEXTURE_SIZE. */
const ATLAS_GRID = 8;
const ATLAS_SIZE = ATLAS_GRID * TEXTURE_SIZE; // 512

/** Near/far clip planes. */
const NEAR = 0.5;
const FAR = 2000;

/** Cylinder approximation — number of sides. */
const CYLINDER_SIDES = 12;

/** Fog start/end distances. */
const FOG_START = 400;
const FOG_END = 1500;

// ── Texture Atlas ───────────────────────────────────────────────

/** Known texture ids — mapped to atlas slots. */
const TEXTURE_IDS = [
	'wall_stone', // 0
	'wall_ruin', // 1
	'roof_tile', // 2
	'ruin_roof', // 3
	'wall_top', // 4
	'tower_top', // 5
	'grass', // 6
	'dirt', // 7
	'sky', // 8
	'tree_top', // 9
	'tree_trunk', // 10
	'road', // 11
	'field', // 12
	'square', // 13
	'wild', // 14
	'house_wall', // 15
	'wall_wood', // 16
	'palisade_top', // 17
	'house_wood', // 18
	'roof_thatch', // 19
	'ground_tundra', // 20
	'ground_taiga', // 21
	'ground_snow', // 22
	'ground_valley', // 23
	'ground_swamp', // 24
	'ground_desert', // 25
	'ground_steppe', // 26
	'ground_tropics', // 27
	'water', // 28
	'shore', // 29
	'ground_rock', // 30
] as const;

type TextureSlot = {u: number; v: number; size: number};

function buildTextureAtlasMap(): Map<string, TextureSlot> {
	const map = new Map<string, TextureSlot>();
	const tileUv = 1 / ATLAS_GRID;
	for (const [i, TEXTURE_ID] of TEXTURE_IDS.entries()) {
		const col = i % ATLAS_GRID;
		const row = Math.floor(i / ATLAS_GRID);
		map.set(TEXTURE_ID, {u: col * tileUv, v: row * tileUv, size: tileUv});
	}

	return map;
}

// ── Shader sources ──────────────────────────────────────────────
// Inline for now — extract to shaders-3d.ts when they grow.

const TERRAIN_VS = `#version 300 es
precision highp float;

uniform mat4 u_viewProj;
uniform sampler2D u_heightmap;
uniform float u_heightScale;
uniform float u_mapSize;

in vec2 a_pos; // grid coords 0..1

out vec3 v_worldPos;
out vec2 v_uv;

void main() {
	vec2 worldXZ = a_pos * u_mapSize;
	float h = texture(u_heightmap, a_pos).r * u_heightScale;
	vec3 pos = vec3(worldXZ.x, h, worldXZ.y);
	v_worldPos = pos;
	v_uv = a_pos;
	gl_Position = u_viewProj * vec4(pos, 1.0);
}
`;

const TERRAIN_FS = `#version 300 es
precision highp float;

uniform sampler2D u_atlas;
uniform sampler2D u_tileGrid;
uniform float u_atlasGrid;
uniform vec3 u_fogColor;
uniform float u_fogStart;
uniform float u_fogEnd;
uniform vec3 u_camPos;

in vec3 v_worldPos;
in vec2 v_uv;

out vec4 fragColor;

// Map tile type → atlas column index
int tileToAtlas(int t) {
	if (t == 0) return 14;
	if (t == 1) return 11;
	if (t == 2) return 7;
	if (t == 3) return 0;
	if (t == 4) return 12;
	if (t == 6) return 13;
	if (t >= 8 && t <= 15) return 20 + (t - 8);
	if (t == 16) return 7;
	if (t == 17) return 29;
	if (t == 18) return 30;
	return 6;
}

void main() {
	float raw = texture(u_tileGrid, v_uv).r;
	int tileType = int(raw * 255.0 + 0.5);
	int atlasIdx = tileToAtlas(tileType);

	float tileSize = 1.0 / u_atlasGrid;
	float col = float(atlasIdx % int(u_atlasGrid));
	float row = float(atlasIdx / int(u_atlasGrid));
	vec2 atlasUv = vec2(col, row) * tileSize + fract(v_uv * 64.0) * tileSize;
	vec3 col3 = texture(u_atlas, atlasUv).rgb;

	float dist = length(v_worldPos - u_camPos);
	float fog = clamp((dist - u_fogStart) / (u_fogEnd - u_fogStart), 0.0, 1.0);
	col3 = mix(col3, u_fogColor, fog);

	fragColor = vec4(col3, 1.0);
}
`;

// ── Water plane shaders ─────────────────────────────────────────
// Universal horizontal plane at waterLevel. Rendered after terrain
// with alpha blending — terrain below the plane is visible through
// as the lake/sea bottom. Animated via u_time.

const WATER_VS = `#version 300 es
precision highp float;

uniform mat4 u_viewProj;
uniform float u_waterY;   // world-space Y of water surface
uniform float u_mapSize;

in vec2 a_pos; // 0..1 grid coords

out vec3 v_worldPos;
out vec2 v_uv;

void main() {
	vec2 worldXZ = a_pos * u_mapSize;
	vec3 pos = vec3(worldXZ.x, u_waterY, worldXZ.y);
	v_worldPos = pos;
	v_uv = worldXZ * 0.02;
	gl_Position = u_viewProj * vec4(pos, 1.0);
}
`;

const WATER_FS = `#version 300 es
precision highp float;

uniform vec3 u_fogColor;
uniform float u_fogStart;
uniform float u_fogEnd;
uniform vec3 u_camPos;
uniform float u_time;

in vec3 v_worldPos;
in vec2 v_uv;

out vec4 fragColor;

void main() {
	// Two-layer sine waves for surface animation
	float w1 = sin(v_uv.x * 6.0 + u_time * 1.4) * sin(v_uv.y * 5.0 + u_time * 1.1);
	float w2 = sin(v_uv.x * 3.7 - u_time * 0.9) * sin(v_uv.y * 4.3 + u_time * 1.3);
	float wave = (w1 + w2) * 0.5;

	// Depth-like color: mix deep/shallow based on wave perturbation
	vec3 deepCol = vec3(0.03, 0.14, 0.30);
	vec3 shallowCol = vec3(0.08, 0.30, 0.42);
	vec3 waterCol = mix(deepCol, shallowCol, wave * 0.5 + 0.5);

	// Specular glint from wave peaks
	float glint = pow(max(0.0, wave), 4.0) * 0.12;
	waterCol += vec3(glint);

	// Fresnel-like transparency: shallow viewing angle → more opaque
	vec3 toEye = normalize(u_camPos - v_worldPos);
	float fresnel = 1.0 - abs(toEye.y);
	float alpha = mix(0.45, 0.82, fresnel * fresnel);

	// Distance fog
	float dist = length(v_worldPos - u_camPos);
	float fog = clamp((dist - u_fogStart) / (u_fogEnd - u_fogStart), 0.0, 1.0);
	waterCol = mix(waterCol, u_fogColor, fog);

	fragColor = vec4(waterCol, alpha);
}
`;

const STRUCTURE_VS = `#version 300 es
precision highp float;

uniform mat4 u_viewProj;
uniform sampler2D u_heightmap;
uniform float u_heightScale;
uniform float u_mapSize;

// Per-vertex
in vec3 a_pos;       // local-space vertex
in vec2 a_uv;

// Per-instance
in vec4 a_instPos;   // xyz = world center, w = rotation
in vec4 a_instSize;  // xyz = half-extents (w/2, height, l/2), w = shape (0=rect, 1=circle)
in vec4 a_instUvWall;// uv offset + size for wall texture
in vec4 a_instUvRoof;// uv offset + size for roof texture
in float a_instState;// 0=active, 1=abandoned, 2=withered

out vec3 v_worldPos;
out vec2 v_uv;
flat out vec4 v_uvWall;
flat out vec4 v_uvRoof;
flat out float v_state;

void main() {
	// Rotate local position by instance rotation (Y-axis)
	float c = cos(a_instPos.w);
	float s = sin(a_instPos.w);
	vec3 scaled = a_pos * a_instSize.xyz;
	vec3 rotated = vec3(
		scaled.x * c - scaled.z * s,
		scaled.y,
		scaled.x * s + scaled.z * c
	);

	// Sample terrain height at structure center
	vec2 hmUv = a_instPos.xz / u_mapSize;
	float baseH = texture(u_heightmap, hmUv).r * u_heightScale;

	vec3 worldPos = rotated + vec3(a_instPos.x, baseH, a_instPos.z);
	v_worldPos = worldPos;
	v_uv = a_uv;
	v_uvWall = a_instUvWall;
	v_uvRoof = a_instUvRoof;
	v_state = a_instState;

	gl_Position = u_viewProj * vec4(worldPos, 1.0);
}
`;

const STRUCTURE_FS = `#version 300 es
precision highp float;

uniform sampler2D u_atlas;
uniform vec3 u_fogColor;
uniform float u_fogStart;
uniform float u_fogEnd;
uniform vec3 u_camPos;

in vec3 v_worldPos;
in vec2 v_uv;
flat in vec4 v_uvWall;
flat in vec4 v_uvRoof;
flat in float v_state;

out vec4 fragColor;

void main() {
	// Detect roof vs wall from face normal (derivative-based)
	vec3 dPdx = dFdx(v_worldPos);
	vec3 dPdy = dFdy(v_worldPos);
	vec3 faceN = normalize(cross(dPdx, dPdy));
	vec4 uvRect = abs(faceN.y) > 0.7 ? v_uvRoof : v_uvWall;
	vec2 atlasUv = uvRect.xy + fract(v_uv) * uvRect.zw;
	vec3 col = texture(u_atlas, atlasUv).rgb;

	// Abandoned/withered tint
	if (v_state > 0.5) {
		col = mix(col, vec3(0.4, 0.35, 0.3), 0.4);
	}

	// Distance fog
	float dist = length(v_worldPos - u_camPos);
	float fog = clamp((dist - u_fogStart) / (u_fogEnd - u_fogStart), 0.0, 1.0);
	col = mix(col, u_fogColor, fog);

	fragColor = vec4(col, 1.0);
}
`;

const BILLBOARD_VS = `#version 300 es
precision highp float;

uniform mat4 u_viewProj;
uniform vec3 u_camRight;
uniform vec3 u_camUp;
uniform sampler2D u_heightmap;
uniform float u_heightScale;
uniform float u_mapSize;

// Per-vertex: quad corners (-1..1)
in vec2 a_corner;

// Per-instance
in vec3 a_spritePos;  // world x, y(unused), z
in vec2 a_spriteSize; // width, height
in vec4 a_spriteColor;
in vec4 a_spriteUv;   // sprite sheet UV rect (u, v, w, h); w=0 → procedural

out vec2 v_uv;
flat out vec4 v_color;
flat out vec4 v_spriteUv;
out vec3 v_worldPos;

void main() {
	// Sample terrain height
	vec2 hmUv = a_spritePos.xz / u_mapSize;
	float baseH = texture(u_heightmap, hmUv).r * u_heightScale;

	// Billboard: expand quad in camera space
	vec3 center = vec3(a_spritePos.x, baseH + a_spriteSize.y * 0.5, a_spritePos.z);
	vec3 worldPos = center
		+ u_camRight * a_corner.x * a_spriteSize.x * 0.5
		+ u_camUp * a_corner.y * a_spriteSize.y * 0.5;

	v_worldPos = worldPos;
	v_uv = a_corner * 0.5 + 0.5;
	v_color = a_spriteColor;
	v_spriteUv = a_spriteUv;

	gl_Position = u_viewProj * vec4(worldPos, 1.0);
}
`;

const BILLBOARD_FS = `#version 300 es
precision highp float;

uniform vec3 u_fogColor;
uniform float u_fogStart;
uniform float u_fogEnd;
uniform vec3 u_camPos;
uniform sampler2D u_spriteTex;
uniform sampler2D u_treeAtlas;
uniform float u_treeTypes;

in vec2 v_uv;
flat in vec4 v_color;
flat in vec4 v_spriteUv;
in vec3 v_worldPos;

out vec4 fragColor;

void main() {
	vec3 col;
	float alpha;

	if (v_spriteUv.z > 0.0) {
		// NPC sprite from sprite sheet
		vec2 uv = v_spriteUv.xy + vec2(v_uv.x, 1.0 - v_uv.y) * v_spriteUv.zw;
		vec4 texel = texture(u_spriteTex, uv);
		col = texel.rgb;
		alpha = texel.a;
	} else if (v_spriteUv.w < -0.5) {
		// Solid-color billboard (NPC without sprite sheet)
		col = v_color.rgb;
		alpha = v_color.a;
	} else {
		// Tree sprite from pre-rendered atlas
		float tp = v_spriteUv.x;
		vec2 uv = vec2((tp + v_uv.x) / u_treeTypes, 1.0 - v_uv.y);
		vec4 texel = texture(u_treeAtlas, uv);
		col = texel.rgb;
		alpha = texel.a;
	}

	if (alpha < 0.5) discard;

	float dist = length(v_worldPos - u_camPos);
	float fog = clamp((dist - u_fogStart) / (u_fogEnd - u_fogStart), 0.0, 1.0);
	col = mix(col, u_fogColor, fog);

	fragColor = vec4(col, alpha);
}
`;

const SKY_VS = `#version 300 es
in vec2 a_pos;
out vec2 v_uv;
void main() {
	v_uv = a_pos * 0.5 + 0.5;
	gl_Position = vec4(a_pos, 0.9999, 1.0);
}
`;

const SKY_FS = `#version 300 es
precision highp float;
uniform vec3 u_fogColor;
out vec4 fragColor;
void main() {
	fragColor = vec4(u_fogColor, 1.0);
}
`;

// ── GL Helpers ──────────────────────────────────────────────────

function compileShader(gl: WebGL2RenderingContext, type: number, src: string): WebGLShader {
	const s = gl.createShader(type)!;
	gl.shaderSource(s, src);
	gl.compileShader(s);
	if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
		const log = gl.getShaderInfoLog(s);
		gl.deleteShader(s);
		throw new Error(`Shader compile error: ${log}`);
	}

	return s;
}

function linkProgram(gl: WebGL2RenderingContext, vs: string, fs: string): WebGLProgram {
	const vShader = compileShader(gl, gl.VERTEX_SHADER, vs);
	const fShader = compileShader(gl, gl.FRAGMENT_SHADER, fs);
	const prog = gl.createProgram();
	gl.attachShader(prog, vShader);
	gl.attachShader(prog, fShader);
	gl.linkProgram(prog);
	if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
		const log = gl.getProgramInfoLog(prog);
		gl.deleteProgram(prog);
		throw new Error(`Program link error: ${log}`);
	}

	gl.deleteShader(vShader);
	gl.deleteShader(fShader);
	return prog;
}

function getUniformLoc(gl: WebGL2RenderingContext, prog: WebGLProgram, name: string): WebGLUniformLocation {
	const loc = gl.getUniformLocation(prog, name);
	if (!loc) {
		console.warn(`Uniform "${name}" not found or optimised out`);
	}

	return loc!;
}

// ── Renderer Class ──────────────────────────────────────────────

/** Entity data for billboard rendering. */
export type BillboardEntity = {
	x: number;
	z: number;
	width: number;
	height: number;
	r: number;
	g: number;
	b: number;
	a: number;
	/** Sprite sheet UV rect [u, v, w, h]. If w=0, uses procedural rendering. */
	spriteUv?: [number, number, number, number];
	/** Tree type index (0–5) for procedural tree rendering. */
	treeType?: number;
};

/** Write one billboard entity into a Float32Array at the given offset. */
function writeBillboard(data: Float32Array, off: number, entity: BillboardEntity): void {
	data[off] = entity.x;
	data[off + 1] = 0;
	data[off + 2] = entity.z;
	data[off + 3] = entity.width;
	data[off + 4] = entity.height;
	data[off + 5] = entity.r;
	data[off + 6] = entity.g;
	data[off + 7] = entity.b;
	data[off + 8] = entity.a;
	const uv = entity.spriteUv;
	if (uv) {
		// NPC with sprite sheet: z > 0 selects sprite path
		data[off + 9] = uv[0];
		data[off + 10] = uv[1];
		data[off + 11] = uv[2];
		data[off + 12] = uv[3];
	} else if (entity.treeType === undefined) {
		// Solid-color NPC: w < 0 selects color path
		data[off + 9] = 0;
		data[off + 10] = 0;
		data[off + 11] = 0;
		data[off + 12] = -1;
	} else {
		// Tree: z == 0, w >= 0 selects tree atlas path
		data[off + 9] = entity.treeType;
		data[off + 10] = 0;
		data[off + 11] = 0;
		data[off + 12] = 0;
	}
}

export class SubworldRenderer3D {
	private readonly gl: WebGL2RenderingContext;
	private readonly mapSize: number;

	// Shader programs
	private terrainProg!: WebGLProgram;
	private waterProg!: WebGLProgram;
	private structureProg!: WebGLProgram;
	private billboardProg!: WebGLProgram;
	private skyProg!: WebGLProgram;

	// Geometry
	private terrainVao!: WebGLVertexArrayObject;
	private terrainIndexCount = 0;
	private waterVao!: WebGLVertexArrayObject;
	private boxVao!: WebGLVertexArrayObject;
	private boxIndexCount = 0;
	private cylinderVao!: WebGLVertexArrayObject;
	private cylinderIndexCount = 0;
	private billboardVao!: WebGLVertexArrayObject;
	private skyVao!: WebGLVertexArrayObject;

	// Instance buffers
	private boxInstanceBuffer!: WebGLBuffer;
	private boxInstanceCount = 0;
	private cylinderInstanceBuffer!: WebGLBuffer;
	private cylinderInstanceCount = 0;
	private billboardInstanceBuffer!: WebGLBuffer;
	private billboardInstanceCount = 0;
	private staticBillboardCount = 0;
	private staticBillboardData: Float32Array | undefined;

	// Textures
	private heightmapTex!: WebGLTexture;
	private atlasTex!: WebGLTexture;
	private tileGridTex!: WebGLTexture;
	private spriteTex!: WebGLTexture;
	private treeAtlasTex!: WebGLTexture;
	private readonly atlasMap!: Map<string, TextureSlot>;

	// State
	private readonly fogColor: Vec3 = [0.6, 0.7, 0.85];
	private disposed = false;
	/** Water surface height in heightmap space (0..1). Set via setWaterLevel(). */
	private waterLevel = 0.3;

	constructor(canvas: HTMLCanvasElement, mapSize = 1024) {
		const gl = canvas.getContext('webgl2', {
			antialias: false,
			alpha: false,
			depth: true,
			stencil: false,
		});
		if (!gl) {
			throw new Error('WebGL2 not supported');
		}

		this.gl = gl;
		this.mapSize = mapSize;

		gl.enable(gl.DEPTH_TEST);
		gl.enable(gl.CULL_FACE);
		gl.cullFace(gl.BACK);
		gl.clearColor(this.fogColor[0], this.fogColor[1], this.fogColor[2], 1);

		// Enable float texture filtering (required for R32F heightmap with LINEAR)
		gl.getExtension('OES_texture_float_linear');

		this.initShaders();
		this.initGeometry();
		this.atlasMap = buildTextureAtlasMap();
		this.initDefaultTileGrid();
		this.initTreeAtlas();
	}

	// ── Initialization ────────────────────────────────────────

	/** Create a default 1×1 tile grid (all grass) as fallback. */
	private initDefaultTileGrid(): void {
		const {gl} = this;
		const tex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, 1, 1, 0, gl.RED, gl.UNSIGNED_BYTE, new Uint8Array([5]));
		gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.bindTexture(gl.TEXTURE_2D, null);
		this.tileGridTex = tex;
	}

	/** Pre-render tree sprite atlas (6 types × 64×64). */
	private initTreeAtlas(): void {
		const {gl} = this;
		const atlas = generateTreeAtlas();
		const tex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, atlas.width, atlas.height, 0, gl.RGBA, gl.UNSIGNED_BYTE, atlas.data);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.bindTexture(gl.TEXTURE_2D, null);
		this.treeAtlasTex = tex;
	}

	private initShaders(): void {
		const {gl} = this;
		this.terrainProg = linkProgram(gl, TERRAIN_VS, TERRAIN_FS);
		this.waterProg = linkProgram(gl, WATER_VS, WATER_FS);
		this.structureProg = linkProgram(gl, STRUCTURE_VS, STRUCTURE_FS);
		this.billboardProg = linkProgram(gl, BILLBOARD_VS, BILLBOARD_FS);
		this.skyProg = linkProgram(gl, SKY_VS, SKY_FS);
	}

	private initGeometry(): void {
		this.buildTerrainMesh();
		this.buildWaterQuad();
		this.buildBoxGeometry();
		this.buildCylinderGeometry();
		this.buildBillboardQuad();
		this.buildSkyQuad();
	}

	// ── Terrain mesh ──────────────────────────────────────────

	private buildTerrainMesh(): void {
		const {gl} = this;
		const resolution = TERRAIN_RES;
		const verts = new Float32Array(resolution * resolution * 2);
		let vi = 0;
		for (let y = 0; y < resolution; y++) {
			for (let x = 0; x < resolution; x++) {
				verts[vi++] = x / (resolution - 1);
				verts[vi++] = y / (resolution - 1);
			}
		}

		const indexCount = (resolution - 1) * (resolution - 1) * 6;
		const indices = new Uint32Array(indexCount);
		let ii = 0;
		for (let y = 0; y < resolution - 1; y++) {
			for (let x = 0; x < resolution - 1; x++) {
				const tl = y * resolution + x;
				const tr = tl + 1;
				const bl = tl + resolution;
				const br = bl + 1;
				indices[ii++] = tl;
				indices[ii++] = bl;
				indices[ii++] = tr;
				indices[ii++] = tr;
				indices[ii++] = bl;
				indices[ii++] = br;
			}
		}

		this.terrainIndexCount = indexCount;

		const vao = gl.createVertexArray();
		gl.bindVertexArray(vao);

		const vbo = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
		gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

		const posLoc = gl.getAttribLocation(this.terrainProg, 'a_pos');
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);

		const ebo = gl.createBuffer();
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ebo);
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);

		gl.bindVertexArray(null);
		this.terrainVao = vao;
	}

	// ── Water plane geometry (simple 2-tri quad, reuses a_pos 0..1) ──

	private buildWaterQuad(): void {
		const {gl} = this;
		const verts = new Float32Array([0, 0, 1, 0, 1, 1, 0, 1]);
		const idx = new Uint16Array([0, 1, 2, 0, 2, 3]);

		const vao = gl.createVertexArray();
		gl.bindVertexArray(vao);

		const vbo = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
		gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);
		const posLoc = gl.getAttribLocation(this.waterProg, 'a_pos');
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);

		const ebo = gl.createBuffer();
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ebo);
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);

		gl.bindVertexArray(null);
		this.waterVao = vao;
	}

	// ── Unit box geometry (1×1×1, centered on XZ, Y from 0 to 1) ──

	private buildBoxGeometry(): void {
		const {gl} = this;
		// 6 faces, each face 4 verts, but share via indices
		// pos (xyz) + uv (xy) per vertex, 24 verts (4 per face)

		const v = new Float32Array([
			// Front face (z=+0.5)  — wall
			-0.5,
			0,
			0.5,
			0,
			0,
			0.5,
			0,
			0.5,
			1,
			0,
			0.5,
			1,
			0.5,
			1,
			1,
			-0.5,
			1,
			0.5,
			0,
			1,
			// Back face (z=-0.5)  — wall
			0.5,
			0,
			-0.5,
			0,
			0,
			-0.5,
			0,
			-0.5,
			1,
			0,
			-0.5,
			1,
			-0.5,
			1,
			1,
			0.5,
			1,
			-0.5,
			0,
			1,
			// Right face (x=+0.5)  — wall
			0.5,
			0,
			0.5,
			0,
			0,
			0.5,
			0,
			-0.5,
			1,
			0,
			0.5,
			1,
			-0.5,
			1,
			1,
			0.5,
			1,
			0.5,
			0,
			1,
			// Left face (x=-0.5)  — wall
			-0.5,
			0,
			-0.5,
			0,
			0,
			-0.5,
			0,
			0.5,
			1,
			0,
			-0.5,
			1,
			0.5,
			1,
			1,
			-0.5,
			1,
			-0.5,
			0,
			1,
			// Top face (y=1)  — roof
			-0.5,
			1,
			-0.5,
			0,
			0,
			-0.5,
			1,
			0.5,
			0,
			1,
			0.5,
			1,
			0.5,
			1,
			1,
			0.5,
			1,
			-0.5,
			1,
			0,
			// Bottom face (y=0) — floor
			-0.5,
			0,
			0.5,
			0,
			0,
			-0.5,
			0,
			-0.5,
			0,
			1,
			0.5,
			0,
			-0.5,
			1,
			1,
			0.5,
			0,
			0.5,
			1,
			0,
		]);

		const idx = new Uint16Array([
			0,
			1,
			2,
			0,
			2,
			3, // Front
			4,
			5,
			6,
			4,
			6,
			7, // Back
			8,
			9,
			10,
			8,
			10,
			11, // Right
			12,
			13,
			14,
			12,
			14,
			15, // Left
			16,
			17,
			18,
			16,
			18,
			19, // Top
			20,
			21,
			22,
			20,
			22,
			23, // Bottom
		]);

		this.boxIndexCount = idx.length;
		const vao = gl.createVertexArray();
		gl.bindVertexArray(vao);

		const vbo = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
		gl.bufferData(gl.ARRAY_BUFFER, v, gl.STATIC_DRAW);

		const stride = 5 * 4;
		const posLoc = gl.getAttribLocation(this.structureProg, 'a_pos');
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 3, gl.FLOAT, false, stride, 0);

		const uvLoc = gl.getAttribLocation(this.structureProg, 'a_uv');
		gl.enableVertexAttribArray(uvLoc);
		gl.vertexAttribPointer(uvLoc, 2, gl.FLOAT, false, stride, 12);

		const ebo = gl.createBuffer();
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ebo);
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);

		// Instance buffer — allocated later in uploadStructures
		this.boxInstanceBuffer = gl.createBuffer()!;

		gl.bindVertexArray(null);
		this.boxVao = vao;
	}

	// ── Unit cylinder geometry (radius 0.5, Y from 0 to 1) ───

	private buildCylinderGeometry(): void {
		const {gl} = this;
		const n = CYLINDER_SIDES;
		// 2 center verts + n*2 ring verts + n*2 side verts
		const verts: number[] = [];
		const indices: number[] = [];

		// Side faces: n quads (each 2 triangles)
		for (let i = 0; i < n; i++) {
			const a0 = (i / n) * Math.PI * 2;
			const a1 = ((i + 1) / n) * Math.PI * 2;
			const x0 = Math.cos(a0) * 0.5;
			const z0 = Math.sin(a0) * 0.5;
			const x1 = Math.cos(a1) * 0.5;
			const z1 = Math.sin(a1) * 0.5;
			const u0 = i / n;
			const u1 = (i + 1) / n;
			const base = verts.length / 5;
			// 4 verts per side quad
			verts.push(x0, 0, z0, u0, 0, x1, 0, z1, u1, 0, x1, 1, z1, u1, 1, x0, 1, z0, u0, 1); // Top-left
			indices.push(base, base + 2, base + 1, base, base + 3, base + 2);
		}

		// Top cap
		const topCenter = verts.length / 5;
		verts.push(0, 1, 0, 0.5, 0.5);
		for (let i = 0; i < n; i++) {
			const a = (i / n) * Math.PI * 2;
			verts.push(Math.cos(a) * 0.5, 1, Math.sin(a) * 0.5, Math.cos(a) * 0.5 + 0.5, Math.sin(a) * 0.5 + 0.5);
		}

		for (let i = 0; i < n; i++) {
			indices.push(topCenter, topCenter + 1 + ((i + 1) % n), topCenter + 1 + i);
		}

		this.cylinderIndexCount = indices.length;
		const vao = gl.createVertexArray();
		gl.bindVertexArray(vao);

		const vbo = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.STATIC_DRAW);

		const stride = 5 * 4;
		const posLoc = gl.getAttribLocation(this.structureProg, 'a_pos');
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 3, gl.FLOAT, false, stride, 0);

		const uvLoc = gl.getAttribLocation(this.structureProg, 'a_uv');
		gl.enableVertexAttribArray(uvLoc);
		gl.vertexAttribPointer(uvLoc, 2, gl.FLOAT, false, stride, 12);

		const ebo = gl.createBuffer();
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ebo);
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(indices), gl.STATIC_DRAW);

		this.cylinderInstanceBuffer = gl.createBuffer()!;

		gl.bindVertexArray(null);
		this.cylinderVao = vao;
	}

	// ── Billboard quad (-1..1) ────────────────────────────────

	private buildBillboardQuad(): void {
		const {gl} = this;
		const corners = new Float32Array([
			-1,
			-1,
			1,
			-1,
			1,
			1,
			-1,
			1,
		]);
		const idx = new Uint16Array([0, 1, 2, 0, 2, 3]);

		const vao = gl.createVertexArray();
		gl.bindVertexArray(vao);

		const vbo = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
		gl.bufferData(gl.ARRAY_BUFFER, corners, gl.STATIC_DRAW);

		const cornerLoc = gl.getAttribLocation(this.billboardProg, 'a_corner');
		gl.enableVertexAttribArray(cornerLoc);
		gl.vertexAttribPointer(cornerLoc, 2, gl.FLOAT, false, 0, 0);

		const ebo = gl.createBuffer();
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ebo);
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);

		this.billboardInstanceBuffer = gl.createBuffer()!;

		gl.bindVertexArray(null);
		this.billboardVao = vao;
	}

	// ── Sky fullscreen quad ───────────────────────────────────

	private buildSkyQuad(): void {
		const {gl} = this;
		const quad = new Float32Array([-1, -1, 1, -1, 1, 1, -1, 1]);
		const idx = new Uint16Array([0, 1, 2, 0, 2, 3]);

		const vao = gl.createVertexArray();
		gl.bindVertexArray(vao);

		const vbo = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
		gl.bufferData(gl.ARRAY_BUFFER, quad, gl.STATIC_DRAW);

		const posLoc = gl.getAttribLocation(this.skyProg, 'a_pos');
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);

		const ebo = gl.createBuffer();
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ebo);
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);

		gl.bindVertexArray(null);
		this.skyVao = vao;
	}

	// ── Data Upload ─────────────────────────────────────────────

	/** Set the water surface level (0..1 in heightmap space). */
	setWaterLevel(level: number): void {
		this.waterLevel = level;
	}

	/** Upload heightmap as a float texture. Call once after map generation. */
	uploadHeightmap(heightmap: Float32Array, width: number, height: number): void {
		const {gl} = this;
		if (this.heightmapTex) {
			gl.deleteTexture(this.heightmapTex);
		}

		const tex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32F, width, height, 0, gl.RED, gl.FLOAT, heightmap);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.bindTexture(gl.TEXTURE_2D, null);
		this.heightmapTex = tex;
	}

	/** Upload tile grid as a single-channel texture for terrain material lookup. */
	uploadTileGrid(tileGrid: Uint8Array, width: number, height: number): void {
		const {gl} = this;
		if (this.tileGridTex) {
			gl.deleteTexture(this.tileGridTex);
		}

		const tex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, width, height, 0, gl.RED, gl.UNSIGNED_BYTE, tileGrid);
		gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.bindTexture(gl.TEXTURE_2D, null);
		this.tileGridTex = tex;
	}

	/** Build and upload the texture atlas from procedural textures. */
	uploadTextureAtlas(): void {
		const {gl} = this;
		if (this.atlasTex) {
			gl.deleteTexture(this.atlasTex);
		}

		// Compose atlas on a canvas
		const canvas = document.createElement('canvas');
		canvas.width = ATLAS_SIZE;
		canvas.height = ATLAS_SIZE;
		const ctx = canvas.getContext('2d')!;

		for (const [i, TEXTURE_ID] of TEXTURE_IDS.entries()) {
			const tex = getTexture(TEXTURE_ID);
			const col = i % ATLAS_GRID;
			const row = Math.floor(i / ATLAS_GRID);
			ctx.putImageData(tex, col * TEXTURE_SIZE, row * TEXTURE_SIZE);
		}

		const tex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, canvas);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.bindTexture(gl.TEXTURE_2D, null);
		this.atlasTex = tex;
	}

	/**
	 * Upload structure instances for rendering.
	 * Separates into boxes (rect) and cylinders (circle).
	 * Sprites are extracted into billboard list.
	 */
	uploadStructures(structures: Structure[]): {billboards: BillboardEntity[]} {
		const boxes: Structure[] = [];
		const cylinders: Structure[] = [];
		const billboards: BillboardEntity[] = [];

		for (const s of structures) {
			if (s.sprite) {
				// Parse spriteColor as tree type index (0–5) or use default
				const tp = Number.parseInt(s.spriteColor ?? '0', 10) || 0;
				billboards.push({
					x: s.x, z: s.y, // Map Y → world Z
					width: 2 + s.height * 0.3,
					height: s.height,
					r: 0, g: 0, b: 0, a: s.state === 'withered' ? 0.5 : 1,
					treeType: tp,
				});
				continue;
			}

			if (s.shape === 'circle') {
				cylinders.push(s);
			} else {
				boxes.push(s);
			}
		}

		this.uploadBoxInstances(boxes);
		this.uploadCylinderInstances(cylinders);

		return {billboards};
	}

	private uploadBoxInstances(boxes: Structure[]): void {
		const {gl} = this;
		// Per instance: pos(xyz)+rot(w), size(xyz)+shape(w), uvWall(xyzw), uvRoof(xyzw), state(f)
		// = 4+4+4+4+1 = 17 floats per instance
		const floatsPerInst = 17;
		const data = new Float32Array(boxes.length * floatsPerInst);

		for (const [i, s] of boxes.entries()) {
			const off = i * floatsPerInst;
			data[off] = s.x;
			data[off + 1] = 0; // Y computed in shader from heightmap
			data[off + 2] = s.y; // Map Y → world Z
			data[off + 3] = s.rotation;
			data[off + 4] = s.w;
			data[off + 5] = s.height;
			data[off + 6] = s.l;
			data[off + 7] = 0; // Shape = rect
			const wSlot = this.getAtlasSlot(s.wallTexture);
			data[off + 8] = wSlot.u;
			data[off + 9] = wSlot.v;
			data[off + 10] = wSlot.size;
			data[off + 11] = wSlot.size;
			const rSlot = this.getAtlasSlot(s.roofTexture);
			data[off + 12] = rSlot.u;
			data[off + 13] = rSlot.v;
			data[off + 14] = rSlot.size;
			data[off + 15] = rSlot.size;
			data[off + 16] = s.state === 'active' ? 0 : (s.state === 'abandoned' ? 1 : 2);
		}

		gl.bindBuffer(gl.ARRAY_BUFFER, this.boxInstanceBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
		this.boxInstanceCount = boxes.length;
	}

	private uploadCylinderInstances(cylinders: Structure[]): void {
		const {gl} = this;
		const floatsPerInst = 17;
		const data = new Float32Array(cylinders.length * floatsPerInst);

		for (const [i, s] of cylinders.entries()) {
			const off = i * floatsPerInst;
			data[off] = s.x;
			data[off + 1] = 0;
			data[off + 2] = s.y;
			data[off + 3] = s.rotation;
			data[off + 4] = s.w; // Diameter as width
			data[off + 5] = s.height;
			data[off + 6] = s.w; // Diameter as length (same)
			data[off + 7] = 1; // Shape = circle
			const wSlot = this.getAtlasSlot(s.wallTexture);
			data[off + 8] = wSlot.u;
			data[off + 9] = wSlot.v;
			data[off + 10] = wSlot.size;
			data[off + 11] = wSlot.size;
			const rSlot = this.getAtlasSlot(s.roofTexture);
			data[off + 12] = rSlot.u;
			data[off + 13] = rSlot.v;
			data[off + 14] = rSlot.size;
			data[off + 15] = rSlot.size;
			data[off + 16] = s.state === 'active' ? 0 : (s.state === 'abandoned' ? 1 : 2);
		}

		gl.bindBuffer(gl.ARRAY_BUFFER, this.cylinderInstanceBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
		this.cylinderInstanceCount = cylinders.length;
	}

	/** Upload billboard entities (called each frame for moving NPCs). */
	/** Upload static tree billboards once (called after uploadStructures). */
	uploadStaticBillboards(entities: BillboardEntity[]): void {
		const {gl} = this;
		const floatsPerInst = 13;
		const data = new Float32Array(entities.length * floatsPerInst);
		for (const [i, entity] of entities.entries()) {
			writeBillboard(data, i * floatsPerInst, entity);
		}

		this.staticBillboardCount = entities.length;
		this.staticBillboardData = data;

		// Allocate buffer with room for static + dynamic (estimate 256 NPCs max)
		gl.bindBuffer(gl.ARRAY_BUFFER, this.billboardInstanceBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, (entities.length + 256) * floatsPerInst * 4, gl.DYNAMIC_DRAW);
		gl.bufferSubData(gl.ARRAY_BUFFER, 0, data);
		this.billboardInstanceCount = entities.length;
	}

	/** Upload dynamic NPC billboards each frame (appended after static). */
	uploadBillboards(entities: BillboardEntity[]): void {
		const {gl} = this;
		const floatsPerInst = 13;
		const data = new Float32Array(entities.length * floatsPerInst);
		for (const [i, entity] of entities.entries()) {
			writeBillboard(data, i * floatsPerInst, entity);
		}

		const total = this.staticBillboardCount + entities.length;
		const byteOffset = this.staticBillboardCount * floatsPerInst * 4;

		gl.bindBuffer(gl.ARRAY_BUFFER, this.billboardInstanceBuffer);
		// Grow buffer if needed
		if (total > this.staticBillboardCount + 256) {
			gl.bufferData(gl.ARRAY_BUFFER, total * floatsPerInst * 4, gl.DYNAMIC_DRAW);
			if (this.staticBillboardData) {
				gl.bufferSubData(gl.ARRAY_BUFFER, 0, this.staticBillboardData);
			}
		}

		gl.bufferSubData(gl.ARRAY_BUFFER, byteOffset, data);
		this.billboardInstanceCount = total;
	}

	/** Upload a sprite sheet (citizenSheet canvas) for billboard texturing. */
	uploadSpriteSheet(canvas: HTMLCanvasElement): void {
		const {gl} = this;
		if (this.spriteTex) {
			gl.deleteTexture(this.spriteTex);
		}

		const tex = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, tex);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, canvas);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.bindTexture(gl.TEXTURE_2D, null);
		this.spriteTex = tex;
	}

	// ── Render ──────────────────────────────────────────────────

	/** Render one frame. Call from requestAnimationFrame loop. */
	render(camera: CameraState, aspect: number): void {
		if (this.disposed) {
			return;
		}

		const {gl} = this;
		const w = gl.canvas.width;
		const h = gl.canvas.height;
		gl.viewport(0, 0, w, h);
		gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

		// Build view-projection matrix
		const proj = mat4Perspective(1.309, aspect, NEAR, FAR);
		const eye: Vec3 = [camera.x, camera.z, camera.y]; // Map Y → world Z
		const cosY = Math.cos(camera.yaw);
		const sinY = Math.sin(camera.yaw);
		const cosP = Math.cos(camera.pitch);
		const sinP = Math.sin(camera.pitch);
		const target: Vec3 = [
			eye[0] + cosY * cosP,
			eye[1] + sinP,
			eye[2] + sinY * cosP,
		];
		const view = mat4LookAt(eye, target, [0, 1, 0]);
		const viewProj = mat4Multiply(proj, view);

		// Camera right/up vectors for billboards
		const camRight: Vec3 = [view[0], view[4], view[8]];
		const camUp: Vec3 = [view[1], view[5], view[9]];
		const camPos: Vec3 = eye;

		// 1. Sky
		this.renderSky();

		// 2. Terrain
		this.renderTerrain(viewProj, camPos);

		// 3. Water plane (semi-transparent, after terrain)
		this.renderWater(viewProj, camPos);

		// 4. Structures (boxes then cylinders)
		this.renderStructures(viewProj, camPos);

		// 5. Billboards (sprites + NPCs) — with alpha blending
		this.renderBillboards(viewProj, camPos, camRight, camUp);
	}

	private renderSky(): void {
		const {gl} = this;
		gl.disable(gl.DEPTH_TEST);
		gl.useProgram(this.skyProg);
		gl.uniform3fv(getUniformLoc(gl, this.skyProg, 'u_fogColor'), this.fogColor);
		gl.bindVertexArray(this.skyVao);
		gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
		gl.bindVertexArray(null);
		gl.enable(gl.DEPTH_TEST);
	}

	private renderTerrain(viewProj: Mat4, camPos: Vec3): void {
		const {gl} = this;
		gl.useProgram(this.terrainProg);

		gl.uniformMatrix4fv(getUniformLoc(gl, this.terrainProg, 'u_viewProj'), false, viewProj);
		gl.uniform1f(getUniformLoc(gl, this.terrainProg, 'u_heightScale'), HEIGHT_SCALE);
		gl.uniform1f(getUniformLoc(gl, this.terrainProg, 'u_mapSize'), this.mapSize);
		gl.uniform3fv(getUniformLoc(gl, this.terrainProg, 'u_fogColor'), this.fogColor);
		gl.uniform1f(getUniformLoc(gl, this.terrainProg, 'u_fogStart'), FOG_START);
		gl.uniform1f(getUniformLoc(gl, this.terrainProg, 'u_fogEnd'), FOG_END);
		gl.uniform3fv(getUniformLoc(gl, this.terrainProg, 'u_camPos'), camPos);

		// Bind heightmap texture unit 0
		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.heightmapTex);
		gl.uniform1i(getUniformLoc(gl, this.terrainProg, 'u_heightmap'), 0);

		// Bind atlas texture unit 1
		gl.activeTexture(gl.TEXTURE1);
		gl.bindTexture(gl.TEXTURE_2D, this.atlasTex);
		gl.uniform1i(getUniformLoc(gl, this.terrainProg, 'u_atlas'), 1);

		// Bind tile grid texture unit 2
		gl.activeTexture(gl.TEXTURE2);
		gl.bindTexture(gl.TEXTURE_2D, this.tileGridTex);
		gl.uniform1i(getUniformLoc(gl, this.terrainProg, 'u_tileGrid'), 2);
		gl.uniform1f(getUniformLoc(gl, this.terrainProg, 'u_atlasGrid'), ATLAS_GRID);

		gl.bindVertexArray(this.terrainVao);
		gl.drawElements(gl.TRIANGLES, this.terrainIndexCount, gl.UNSIGNED_INT, 0);
		gl.bindVertexArray(null);
	}

	private renderWater(viewProj: Mat4, camPos: Vec3): void {
		const {gl} = this;
		const waterY = this.waterLevel * HEIGHT_SCALE;
		const time = performance.now() * 0.001; // Seconds since page load

		gl.useProgram(this.waterProg);
		gl.uniformMatrix4fv(getUniformLoc(gl, this.waterProg, 'u_viewProj'), false, viewProj);
		gl.uniform1f(getUniformLoc(gl, this.waterProg, 'u_waterY'), waterY);
		gl.uniform1f(getUniformLoc(gl, this.waterProg, 'u_mapSize'), this.mapSize);
		gl.uniform3fv(getUniformLoc(gl, this.waterProg, 'u_fogColor'), this.fogColor);
		gl.uniform1f(getUniformLoc(gl, this.waterProg, 'u_fogStart'), FOG_START);
		gl.uniform1f(getUniformLoc(gl, this.waterProg, 'u_fogEnd'), FOG_END);
		gl.uniform3fv(getUniformLoc(gl, this.waterProg, 'u_camPos'), camPos);
		gl.uniform1f(getUniformLoc(gl, this.waterProg, 'u_time'), time);

		// Alpha blending for translucent water; disable culling (flat plane, visible from both sides)
		gl.enable(gl.BLEND);
		gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
		gl.depthMask(false);
		gl.disable(gl.CULL_FACE);

		gl.bindVertexArray(this.waterVao);
		gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
		gl.bindVertexArray(null);

		gl.enable(gl.CULL_FACE);
		gl.depthMask(true);
		gl.disable(gl.BLEND);
	}

	private renderStructures(viewProj: Mat4, camPos: Vec3): void {
		const {gl} = this;
		gl.useProgram(this.structureProg);

		gl.uniformMatrix4fv(getUniformLoc(gl, this.structureProg, 'u_viewProj'), false, viewProj);
		gl.uniform1f(getUniformLoc(gl, this.structureProg, 'u_heightScale'), HEIGHT_SCALE);
		gl.uniform1f(getUniformLoc(gl, this.structureProg, 'u_mapSize'), this.mapSize);
		gl.uniform3fv(getUniformLoc(gl, this.structureProg, 'u_fogColor'), this.fogColor);
		gl.uniform1f(getUniformLoc(gl, this.structureProg, 'u_fogStart'), FOG_START);
		gl.uniform1f(getUniformLoc(gl, this.structureProg, 'u_fogEnd'), FOG_END);
		gl.uniform3fv(getUniformLoc(gl, this.structureProg, 'u_camPos'), camPos);

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.heightmapTex);
		gl.uniform1i(getUniformLoc(gl, this.structureProg, 'u_heightmap'), 0);

		gl.activeTexture(gl.TEXTURE1);
		gl.bindTexture(gl.TEXTURE_2D, this.atlasTex);
		gl.uniform1i(getUniformLoc(gl, this.structureProg, 'u_atlas'), 1);

		// Draw boxes (instanced)
		if (this.boxInstanceCount > 0) {
			this.bindStructureInstances(this.boxVao, this.boxInstanceBuffer);
			gl.drawElementsInstanced(gl.TRIANGLES, this.boxIndexCount, gl.UNSIGNED_SHORT, 0, this.boxInstanceCount);
			gl.bindVertexArray(null);
		}

		// Draw cylinders (instanced)
		if (this.cylinderInstanceCount > 0) {
			this.bindStructureInstances(this.cylinderVao, this.cylinderInstanceBuffer);
			gl.drawElementsInstanced(gl.TRIANGLES, this.cylinderIndexCount, gl.UNSIGNED_SHORT, 0, this.cylinderInstanceCount);
			gl.bindVertexArray(null);
		}
	}

	private bindStructureInstances(vao: WebGLVertexArrayObject, instanceBuffer: WebGLBuffer): void {
		const {gl} = this;
		gl.bindVertexArray(vao);
		gl.bindBuffer(gl.ARRAY_BUFFER, instanceBuffer);

		const stride = 17 * 4;
		const prog = this.structureProg;

		// A_instPos (vec4)
		const posLoc = gl.getAttribLocation(prog, 'a_instPos');
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 4, gl.FLOAT, false, stride, 0);
		gl.vertexAttribDivisor(posLoc, 1);

		// A_instSize (vec4)
		const sizeLoc = gl.getAttribLocation(prog, 'a_instSize');
		gl.enableVertexAttribArray(sizeLoc);
		gl.vertexAttribPointer(sizeLoc, 4, gl.FLOAT, false, stride, 16);
		gl.vertexAttribDivisor(sizeLoc, 1);

		// A_instUvWall (vec4)
		const uvWallLoc = gl.getAttribLocation(prog, 'a_instUvWall');
		gl.enableVertexAttribArray(uvWallLoc);
		gl.vertexAttribPointer(uvWallLoc, 4, gl.FLOAT, false, stride, 32);
		gl.vertexAttribDivisor(uvWallLoc, 1);

		// A_instUvRoof (vec4)
		const uvRoofLoc = gl.getAttribLocation(prog, 'a_instUvRoof');
		gl.enableVertexAttribArray(uvRoofLoc);
		gl.vertexAttribPointer(uvRoofLoc, 4, gl.FLOAT, false, stride, 48);
		gl.vertexAttribDivisor(uvRoofLoc, 1);

		// A_instState (float)
		const stateLoc = gl.getAttribLocation(prog, 'a_instState');
		gl.enableVertexAttribArray(stateLoc);
		gl.vertexAttribPointer(stateLoc, 1, gl.FLOAT, false, stride, 64);
		gl.vertexAttribDivisor(stateLoc, 1);
	}

	private renderBillboards(viewProj: Mat4, camPos: Vec3, camRight: Vec3, camUp: Vec3): void {
		if (this.billboardInstanceCount === 0) {
			return;
		}

		const {gl} = this;
		gl.useProgram(this.billboardProg);
		gl.disable(gl.CULL_FACE);

		gl.uniformMatrix4fv(getUniformLoc(gl, this.billboardProg, 'u_viewProj'), false, viewProj);
		gl.uniform3fv(getUniformLoc(gl, this.billboardProg, 'u_camRight'), camRight);
		gl.uniform3fv(getUniformLoc(gl, this.billboardProg, 'u_camUp'), camUp);
		gl.uniform3fv(getUniformLoc(gl, this.billboardProg, 'u_fogColor'), this.fogColor);
		gl.uniform1f(getUniformLoc(gl, this.billboardProg, 'u_fogStart'), FOG_START);
		gl.uniform1f(getUniformLoc(gl, this.billboardProg, 'u_fogEnd'), FOG_END);
		gl.uniform3fv(getUniformLoc(gl, this.billboardProg, 'u_camPos'), camPos);
		gl.uniform1f(getUniformLoc(gl, this.billboardProg, 'u_heightScale'), HEIGHT_SCALE);
		gl.uniform1f(getUniformLoc(gl, this.billboardProg, 'u_mapSize'), this.mapSize);

		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.heightmapTex);
		gl.uniform1i(getUniformLoc(gl, this.billboardProg, 'u_heightmap'), 0);

		gl.activeTexture(gl.TEXTURE1);
		gl.bindTexture(gl.TEXTURE_2D, this.spriteTex ?? null);
		gl.uniform1i(getUniformLoc(gl, this.billboardProg, 'u_spriteTex'), 1);

		gl.activeTexture(gl.TEXTURE2);
		gl.bindTexture(gl.TEXTURE_2D, this.treeAtlasTex);
		gl.uniform1i(getUniformLoc(gl, this.billboardProg, 'u_treeAtlas'), 2);
		gl.uniform1f(getUniformLoc(gl, this.billboardProg, 'u_treeTypes'), TREE_TYPES);

		gl.bindVertexArray(this.billboardVao);
		gl.bindBuffer(gl.ARRAY_BUFFER, this.billboardInstanceBuffer);

		const stride = 13 * 4;
		const prog = this.billboardProg;

		const posLoc = gl.getAttribLocation(prog, 'a_spritePos');
		gl.enableVertexAttribArray(posLoc);
		gl.vertexAttribPointer(posLoc, 3, gl.FLOAT, false, stride, 0);
		gl.vertexAttribDivisor(posLoc, 1);

		const sizeLoc = gl.getAttribLocation(prog, 'a_spriteSize');
		gl.enableVertexAttribArray(sizeLoc);
		gl.vertexAttribPointer(sizeLoc, 2, gl.FLOAT, false, stride, 12);
		gl.vertexAttribDivisor(sizeLoc, 1);

		const colorLoc = gl.getAttribLocation(prog, 'a_spriteColor');
		gl.enableVertexAttribArray(colorLoc);
		gl.vertexAttribPointer(colorLoc, 4, gl.FLOAT, false, stride, 20);
		gl.vertexAttribDivisor(colorLoc, 1);

		const uvLoc = gl.getAttribLocation(prog, 'a_spriteUv');
		gl.enableVertexAttribArray(uvLoc);
		gl.vertexAttribPointer(uvLoc, 4, gl.FLOAT, false, stride, 36);
		gl.vertexAttribDivisor(uvLoc, 1);

		gl.drawElementsInstanced(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0, this.billboardInstanceCount);

		gl.bindVertexArray(null);
		gl.enable(gl.CULL_FACE);
	}

	// ── Helpers ─────────────────────────────────────────────────

	private getAtlasSlot(textureId: string): TextureSlot {
		return this.atlasMap.get(textureId) ?? {u: 0, v: 0, size: 1 / ATLAS_GRID};
	}

	// ── Cleanup ─────────────────────────────────────────────────

	dispose(): void {
		if (this.disposed) {
			return;
		}

		this.disposed = true;
		const {gl} = this;
		gl.deleteProgram(this.terrainProg);
		gl.deleteProgram(this.waterProg);
		gl.deleteProgram(this.structureProg);
		gl.deleteProgram(this.billboardProg);
		gl.deleteProgram(this.skyProg);
		if (this.heightmapTex) {
			gl.deleteTexture(this.heightmapTex);
		}

		if (this.atlasTex) {
			gl.deleteTexture(this.atlasTex);
		}

		if (this.tileGridTex) {
			gl.deleteTexture(this.tileGridTex);
		}

		if (this.spriteTex) {
			gl.deleteTexture(this.spriteTex);
		}

		if (this.treeAtlasTex) {
			gl.deleteTexture(this.treeAtlasTex);
		}

		// VAOs and buffers cleaned up by context loss
	}
}

/* eslint-disable @typescript-eslint/no-restricted-types */
import type {
	AtlasData, PaletteConfig, Category, Direction, CharacterData, AnimationState,
} from './types';
import {
	getAtlas, getSheetOrdinal, getEntryIndex,
} from './atlas-loader';
import {paletteManager} from './palette';
import {buildSpritePath, formatSpriteIndex, toPrimarySpriteCategory} from './category-mapping';
import {SPRITE_ORDER, SECONDARY_MIRROR_MAP} from './sprite-data';
import {ANIMATION_FRAME_COUNTS, ANIMATION_START_INDICES} from './animation-constants';
import {clampSpriteIndex} from './sprite-counts';
import zIndexLibrary from './z-index-library.json';

const VERT_SRC = `#version 300 es
in vec2 a_quad;

uniform vec2 u_resolution;
uniform vec2 u_translation;
uniform float u_scale;
uniform vec2 u_atlasSize;
uniform vec2 u_spriteUV;
uniform vec2 u_spriteSize;
uniform vec2 u_cropOffset;

out vec2 v_texCoord;
flat out vec4 v_spriteRect; // xy = uvMin, zw = uvMax (in atlas UV space)

void main() {
  vec2 pos = (a_quad * u_spriteSize + u_cropOffset) * u_scale + u_translation;
  gl_Position = vec4((pos / u_resolution) * 2.0 - 1.0, 0.0, 1.0);
  gl_Position.y *= -1.0;
  v_texCoord = (u_spriteUV + a_quad * u_spriteSize) / u_atlasSize;
  // Pass sprite rect bounds for fragment-shader clamping (inset by half texel)
  vec2 halfTexel = 0.5 / u_atlasSize;
  v_spriteRect = vec4(
    u_spriteUV / u_atlasSize + halfTexel,
    (u_spriteUV + u_spriteSize) / u_atlasSize - halfTexel
  );
}
`;

const FRAG_SRC = `#version 300 es
precision highp float;

uniform sampler2D u_atlas;
uniform vec4 u_grayscale_palette[6];
uniform vec4 u_palette[6];
uniform int u_color_count;
uniform float u_nightDarken;

in vec2 v_texCoord;
flat in vec4 v_spriteRect;
out vec4 fragColor;

void main() {
  // Clamp UV to sprite rect to prevent sampling adjacent sprites
  vec2 uv = clamp(v_texCoord, v_spriteRect.xy, v_spriteRect.zw);
  vec4 src = texture(u_atlas, uv);
  if (src.a < 0.01) discard;
  vec4 col = src;

  if (abs(src.r - src.g) < 0.01 && abs(src.g - src.b) < 0.01) {
    for (int i = 0; i < 6; i++) {
      if (i >= u_color_count) break;
      if (distance(src.rgb, u_grayscale_palette[i].rgb) < 0.05) {
        col = u_palette[i];
        col.a *= src.a;
        break;
      }
    }
  }

  // Apply night darkening (consistent with game renderer)
  if (u_nightDarken > 0.0) {
    vec3 nightTint = vec3(0.05, 0.05, 0.15);
    col.rgb = mix(col.rgb, nightTint, u_nightDarken * 0.82);
  }

  fragColor = col;
}
`;

export class CharacterRenderer {
	private program: WebGLProgram | null = null;
	private quadBuffer: WebGLBuffer | null = null;
	private atlasTexture: WebGLTexture | null = null;
	private atlasUploaded = false;
	private nightDarken = 0;
	private _diagDraws = 0;
	private _diagChars = 0;
	private _diagSkipped = 0;

	// Uniform locations
	private uResolution!: WebGLUniformLocation | null;
	private uTranslation!: WebGLUniformLocation | null;
	private uScale!: WebGLUniformLocation | null;
	private uAtlasSize!: WebGLUniformLocation | null;
	private uSpriteUV!: WebGLUniformLocation | null;
	private uSpriteSize!: WebGLUniformLocation | null;
	private uCropOffset!: WebGLUniformLocation | null;
	private uAtlas!: WebGLUniformLocation | null;
	private uGrayscalePalette!: WebGLUniformLocation | null;
	private uPalette!: WebGLUniformLocation | null;
	private uColorCount!: WebGLUniformLocation | null;
	private uNightDarken!: WebGLUniformLocation | null;

	// Caches
	private readonly paletteVecCache = new Map<string, Float32Array>();
	private readonly hexCache = new Map<string, {r: number; g: number; b: number}>();
	private readonly spriteOrderCache = new Map<string, Category[]>();
	private readonly zIndexMap: Record<string, Record<Direction, number>>;

	constructor(private readonly gl: WebGL2RenderingContext) {
		this.zIndexMap = zIndexLibrary as Record<string, Record<Direction, number>>;
		this.initProgram();
	}

	private initProgram(): void {
		const {gl} = this;
		const vs = this.compileShader(gl.VERTEX_SHADER, VERT_SRC);
		const fs = this.compileShader(gl.FRAGMENT_SHADER, FRAG_SRC);
		if (!vs || !fs) {
			throw new Error('Failed to compile character shaders');
		}

		this.program = gl.createProgram()!;
		gl.attachShader(this.program, vs);
		gl.attachShader(this.program, fs);
		gl.linkProgram(this.program);
		if (!gl.getProgramParameter(this.program, gl.LINK_STATUS)) {
			throw new Error('Character shader link failed: ' + gl.getProgramInfoLog(this.program));
		}

		// Unit-quad buffer (triangle strip, 0-1 range)
		this.quadBuffer = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]), gl.STATIC_DRAW);

		// Cache uniform locations
		const u = (name: string) => gl.getUniformLocation(this.program!, name);
		this.uResolution = u('u_resolution');
		this.uTranslation = u('u_translation');
		this.uScale = u('u_scale');
		this.uAtlasSize = u('u_atlasSize');
		this.uSpriteUV = u('u_spriteUV');
		this.uSpriteSize = u('u_spriteSize');
		this.uCropOffset = u('u_cropOffset');
		this.uAtlas = u('u_atlas');
		this.uGrayscalePalette = u('u_grayscale_palette');
		this.uPalette = u('u_palette');
		this.uColorCount = u('u_color_count');
		this.uNightDarken = u('u_nightDarken');
	}

	private compileShader(type: number, source: string): WebGLShader | null {
		const {gl} = this;
		const shader = gl.createShader(type);
		if (!shader) {
			return null;
		}

		gl.shaderSource(shader, source);
		gl.compileShader(shader);
		if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
			console.error(gl.getShaderInfoLog(shader));
			gl.deleteShader(shader);
			return null;
		}

		return shader;
	}

	uploadAtlas(atlas: AtlasData): void {
		if (this.atlasUploaded) {
			return;
		}

		const {gl} = this;

		// Use a high texture unit to avoid conflicts with game renderer
		this.atlasTexture = gl.createTexture();
		gl.activeTexture(gl.TEXTURE4);
		gl.bindTexture(gl.TEXTURE_2D, this.atlasTexture);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, atlas.image);
		this.atlasUploaded = true;
	}

	get isAtlasUploaded(): boolean {
		return this.atlasUploaded;
	}

	getDiagnostics(): {chars: number; draws: number; skipped: number} {
		return {chars: this._diagChars, draws: this._diagDraws, skipped: this._diagSkipped};
	}

	resetDiagnostics(): void {
		this._diagChars = 0;
		this._diagDraws = 0;
		this._diagSkipped = 0;
	}

	forceReupload(): void {
		this.atlasUploaded = false;
	}

	setNightDarken(factor: number): void {
		this.nightDarken = Math.max(0, Math.min(1, factor));
	}

	/**
	 * Draw a complete character at the given screen position.
	 * Call this between game renderer passes — GL state will be saved/restored.
	 */
	drawCharacter(
		character: CharacterData,
		animState: AnimationState,
		screenX: number,
		screenY: number,
		scale: number,
		canvasWidth: number,
		canvasHeight: number,
	): void {
		const atlas = getAtlas();
		if (!atlas || !this.program) {
			return;
		}

		// Lazy upload: ensure atlas texture is on the GPU
		if (!this.atlasUploaded) {
			this.uploadAtlas(atlas);
		}

		const {gl} = this;

		// Save GL state
		const previousProgram = gl.getParameter(gl.CURRENT_PROGRAM) as WebGLProgram | null;
		const previousBlend = gl.isEnabled(gl.BLEND);

		// Reset vertex attribute state to avoid interference from instanced sprite pass
		for (let i = 0; i < 8; i++) {
			gl.disableVertexAttribArray(i);
			gl.vertexAttribDivisor(i, 0);
		}

		gl.useProgram(this.program);
		gl.enable(gl.BLEND);
		gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

		gl.uniform2f(this.uResolution, canvasWidth, canvasHeight);
		gl.uniform1f(this.uNightDarken, this.nightDarken);

		// Bind character atlas on texture unit 4
		gl.activeTexture(gl.TEXTURE4);
		gl.bindTexture(gl.TEXTURE_2D, this.atlasTexture);
		gl.uniform1i(this.uAtlas, 4);

		// Bind quad buffer
		const aQuad = gl.getAttribLocation(this.program, 'a_quad');
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadBuffer);
		gl.enableVertexAttribArray(aQuad);
		gl.vertexAttribPointer(aQuad, 2, gl.FLOAT, false, 0, 0);

		// Mirror secondary sprites
		for (const [primary, secondary] of Object.entries(SECONDARY_MIRROR_MAP)) {
			const primIndex = character.sprites[primary];
			if (primIndex !== undefined && character.sprites[secondary] !== undefined) {
				character.sprites[secondary] = clampSpriteIndex(secondary, primIndex);
			}
		}

		const direction = animState.currentDirection;
		const animation = animState.currentAnimation;
		const animationFrame = animState.currentFrame;
		const hiddenSet = new Set(character.hidden ?? []);
		const spriteOrder = this.getSpriteRenderOrder(direction);

		// Ensure palette data is loaded
		paletteManager.loadPalettes();

		let charLayersDrawn = 0;
		for (const category of spriteOrder) {
			const spriteIndex = character.sprites[category];
			if (spriteIndex === undefined || hiddenSet.has(category)) {
				continue;
			}

			const mappedCategory = toPrimarySpriteCategory(category);
			const clampedIndex = clampSpriteIndex(mappedCategory, spriteIndex);
			const entryIdx = this.resolveEntryIndex(atlas, mappedCategory, clampedIndex, animation, direction, animationFrame);
			if (entryIdx < 0) {
				this._diagSkipped++;
				continue;
			}

			const paletteConfig = paletteManager.getPaletteConfig(category, character.paletteState);
			this.drawLayer(entryIdx, atlas, paletteConfig, screenX, screenY, scale);
			charLayersDrawn++;
		}

		this._diagChars++;
		this._diagDraws += charLayersDrawn;

		// Restore GL state
		gl.disableVertexAttribArray(aQuad);
		if (!previousBlend) {
			gl.disable(gl.BLEND);
		}

		if (previousProgram) {
			gl.useProgram(previousProgram);
		}
	}

	private drawLayer(
		entryIndex: number,
		atlas: AtlasData,
		palette: PaletteConfig | undefined,
		x: number,
		y: number,
		scale: number,
	): void {
		const {gl} = this;
		if (!this.program) {
			return;
		}

		// Fetch entry from CPU-side data
		const base = entryIndex * 8;
		const w = atlas.entries[base + 2];
		const h = atlas.entries[base + 3];
		if (w === 0 || h === 0) {
			return; // Transparent tile
		}

		const u0 = atlas.entries[base];
		const v0 = atlas.entries[base + 1];
		const ox = atlas.entries[base + 4];
		const oy = atlas.entries[base + 5];

		// Atlas size
		gl.uniform2f(this.uAtlasSize, atlas.atlasWidth, atlas.atlasHeight);

		// Sprite rect in atlas
		gl.uniform2f(this.uSpriteUV, u0, v0);
		gl.uniform2f(this.uSpriteSize, w, h);
		gl.uniform2f(this.uCropOffset, ox, oy);

		// Transform
		gl.uniform2f(this.uTranslation, x, y);
		gl.uniform1f(this.uScale, scale);

		// Palette
		this.applyPalette(palette);

		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
	}

	private resolveEntryIndex(
		atlas: AtlasData,
		mappedCategory: Category,
		clampedIndex: number,
		animation: string,
		direction: string,
		animationFrame: number,
	): number {
		const spritePath = buildSpritePath(mappedCategory, formatSpriteIndex(mappedCategory, clampedIndex));
		const sheetOrd = getSheetOrdinal(atlas, spritePath);
		if (sheetOrd < 0) {
			return -1;
		}

		const tileIndex = this.calculateFrameIndex(animation, direction, animationFrame);
		return getEntryIndex(sheetOrd, tileIndex);
	}

	private calculateFrameIndex(animation: string, direction: string, animationFrame: number): number {
		const directionOrder = ['front', 'back', 'left', 'right'];
		const directionIndex = directionOrder.indexOf(direction);

		if (directionIndex === -1) {
			return 0;
		}

		const frameCount = ANIMATION_FRAME_COUNTS[animation] ?? 4;
		const startIndex = ANIMATION_START_INDICES[animation] ?? 0;

		const frameIndex = startIndex + (directionIndex * frameCount) + (animationFrame % frameCount);
		return Math.min(frameIndex, 159);
	}

	private getSpriteRenderOrder(direction: Direction): Category[] {
		const cached = this.spriteOrderCache.get(direction);
		if (cached) {
			return cached;
		}

		// Build stable order by grouping categories by z-index, then iterating
		// z-index ascending while preserving child order inside each group.
		const zGroups: Record<number, Category[]> = {};
		for (const cat of SPRITE_ORDER) {
			const z = this.zIndexMap[cat]?.[direction] ?? 0;
			zGroups[z] ||= [];
			zGroups[z].push(cat);
		}

		const sortedZ = Object.keys(zGroups)
			.map(Number)
			.sort((a, b) => a - b);

		const result = sortedZ.flatMap(z => zGroups[z]);
		this.spriteOrderCache.set(direction, result);
		return result;
	}

	private applyPalette(config: PaletteConfig | undefined): void {
		const {gl} = this;
		if (!config) {
			gl.uniform1i(this.uColorCount, 0);
			return;
		}

		const grayscale = this.hexColorsToVec4(config.grayscaleColors);
		const colors = this.hexColorsToVec4(config.colors);
		const count = Math.min(config.colorCount, 6, config.grayscaleColors.length, config.colors.length);

		gl.uniform4fv(this.uGrayscalePalette, grayscale);
		gl.uniform4fv(this.uPalette, colors);
		gl.uniform1i(this.uColorCount, count);
	}

	private hexColorsToVec4(hexColors: string[]): Float32Array {
		const key = hexColors.join(',');
		const cached = this.paletteVecCache.get(key);
		if (cached) {
			return cached;
		}

		const result = new Float32Array(24);
		for (let i = 0; i < Math.min(hexColors.length, 6); i++) {
			const color = this.hexToRgb(hexColors[i] ?? '000000');
			result[i * 4] = color.r;
			result[i * 4 + 1] = color.g;
			result[i * 4 + 2] = color.b;
			result[i * 4 + 3] = 1;
		}

		this.paletteVecCache.set(key, result);
		return result;
	}

	private hexToRgb(hex: string): {r: number; g: number; b: number} {
		const cached = this.hexCache.get(hex);
		if (cached) {
			return cached;
		}

		const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
		const parsed = result
			? {
				r: Number.parseInt(result[1] ?? '0', 16) / 255,
				g: Number.parseInt(result[2] ?? '0', 16) / 255,
				b: Number.parseInt(result[3] ?? '0', 16) / 255,
			}
			: {r: 0, g: 0, b: 0};
		this.hexCache.set(hex, parsed);
		return parsed;
	}

	destroy(): void {
		const {gl} = this;
		if (this.program) {
			gl.deleteProgram(this.program);
		}

		if (this.quadBuffer) {
			gl.deleteBuffer(this.quadBuffer);
		}

		if (this.atlasTexture) {
			gl.deleteTexture(this.atlasTexture);
		}

		this.program = null;
		this.quadBuffer = null;
		this.atlasTexture = null;
		this.atlasUploaded = false;
	}
}

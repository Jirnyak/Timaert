/**
 * Pre-renders citizen sprites via an offscreen WebGL2 canvas
 * so the Canvas2D subworld renderer can draw them with drawImage.
 *
 * Each character is rendered as a strip of animation frames:
 *   4 directions × 6 walk frames = 24 cells per character.
 * The player sprite uses the same layout so it animates identically.
 *
 * Direction order in the strip: front, back, left, right
 * (matches the Direction type from character/types).
 */

import {getAtlas, loadAtlas, LOGICAL_TILE_SIZE} from '../../character/atlas-loader';
import {CharacterManager} from '../../character/character-generator';
import {CharacterRenderer} from '../../character/renderer';
import {DEFAULT_PALETTE_STATE} from '../../character/defaults';
import type {AnimationState, CharacterData, Direction} from '../../character/types';

// ── Public type ─────────────────────────────────────────────────

export type CitizenSpriteSheet = {
	/** 2D canvas containing all pre-rendered citizen sprites. */
	canvas: HTMLCanvasElement;
	/** Number of columns in the sheet grid. */
	columns: number;
	/** Pixel size of each sprite cell (square). */
	spriteSize: number;
	/** Total number of unique characters in the sheet. */
	count: number;
	/** Number of walk frames per direction. */
	framesPerDirection: number;
	/** Direction order: index → Direction name. */
	directions: readonly Direction[];
};

// ── Constants ───────────────────────────────────────────────────

const SPRITE_SIZE = LOGICAL_TILE_SIZE; // 48
const MAX_UNIQUE = 50;

/** Walk animation has 6 frames in the character system. */
const WALK_FRAMES = 6;

/** Directions rendered per character, in order. */
const DIRECTIONS: readonly Direction[] = ['front', 'back', 'left', 'right'];

/** Cells per character = directions × frames. */
const CELLS_PER_CHARACTER = DIRECTIONS.length * WALK_FRAMES; // 24

/**
 * Sheet layout: one row per character, each row is
 * CELLS_PER_CHARACTER wide.
 */
const SHEET_COLUMNS = CELLS_PER_CHARACTER;

// ── Helpers ─────────────────────────────────────────────────────

function makeAnimState(
	direction: Direction,
	frame: number,
): AnimationState {
	return {
		currentAnimation: 'walk',
		currentDirection: direction,
		currentFrame: frame,
		frameTimer: 0,
		isPlaying: false,
	};
}

/**
 * Render a single character's full animation strip into the WebGL
 * context at the specified row.  Each row contains 24 cells:
 * [front×6, back×6, left×6, right×6].
 */
function renderCharacterStrip(
	renderer: CharacterRenderer,
	character: CharacterData,
	row: number,
	sheetWidth: number,
	sheetHeight: number,
): void {
	let cellIndex = 0;
	for (const direction of DIRECTIONS) {
		for (let frame = 0; frame < WALK_FRAMES; frame++) {
			const animState = makeAnimState(direction, frame);
			renderer.drawCharacter(
				character,
				animState,
				cellIndex * SPRITE_SIZE,
				row * SPRITE_SIZE,
				1,
				sheetWidth,
				sheetHeight,
			);
			cellIndex++;
		}
	}
}

// ── Factory ─────────────────────────────────────────────────────

/**
 * Generate a sprite sheet of unique citizen appearances with full
 * walk animation frames in all 4 directions.
 */
export async function createCitizenSpriteSheet(population: number): Promise<CitizenSpriteSheet> {
	const atlas = getAtlas() ?? await loadAtlas();

	const uniqueCount = Math.min(MAX_UNIQUE, Math.max(1, population));
	const columns = SHEET_COLUMNS;
	const rows = uniqueCount;
	const sheetWidth = columns * SPRITE_SIZE;
	const sheetHeight = rows * SPRITE_SIZE;

	// Offscreen WebGL2 canvas for character rendering
	const offscreen = document.createElement('canvas');
	offscreen.width = sheetWidth;
	offscreen.height = sheetHeight;

	const glContext = offscreen.getContext('webgl2', {
		alpha: true,
		premultipliedAlpha: false,
		preserveDrawingBuffer: true,
	});

	if (!glContext) {
		throw new Error('WebGL2 not available for citizen sprite rendering');
	}

	glContext.viewport(0, 0, sheetWidth, sheetHeight);
	glContext.clearColor(0, 0, 0, 0);
	glContext.clear(glContext.COLOR_BUFFER_BIT);

	const charRenderer = new CharacterRenderer(glContext);
	charRenderer.uploadAtlas(atlas);

	// Generate and render each unique character
	for (let i = 0; i < uniqueCount; i++) {
		const character = CharacterManager.generateRandomCharacter(DEFAULT_PALETTE_STATE);
		renderCharacterStrip(charRenderer, character, i, sheetWidth, sheetHeight);
	}

	// Copy WebGL result to a 2D canvas for use with Canvas2D drawImage
	const result = document.createElement('canvas');
	result.width = sheetWidth;
	result.height = sheetHeight;
	const context = result.getContext('2d');
	if (!context) {
		throw new Error('Failed to create 2D context for citizen sprites');
	}

	context.drawImage(offscreen, 0, 0);

	return {
		canvas: result,
		columns,
		spriteSize: SPRITE_SIZE,
		count: uniqueCount,
		framesPerDirection: WALK_FRAMES,
		directions: DIRECTIONS,
	};
}

// ── Player sprite helper ────────────────────────────────────────

/**
 * Pre-render the player character with full walk animation frames
 * in all 4 directions, matching the citizen sprite strip layout.
 * Returns a single-row strip (24 cells wide × 1 row tall).
 */
export async function renderPlayerSprite(characterData: CharacterData): Promise<CitizenSpriteSheet> {
	const atlas = getAtlas() ?? await loadAtlas();

	const columns = SHEET_COLUMNS;
	const sheetWidth = columns * SPRITE_SIZE;
	const sheetHeight = SPRITE_SIZE; // Single row

	const offscreen = document.createElement('canvas');
	offscreen.width = sheetWidth;
	offscreen.height = sheetHeight;

	const glContext = offscreen.getContext('webgl2', {
		alpha: true,
		premultipliedAlpha: false,
		preserveDrawingBuffer: true,
	});

	if (!glContext) {
		throw new Error('WebGL2 not available for player sprite rendering');
	}

	glContext.viewport(0, 0, sheetWidth, sheetHeight);
	glContext.clearColor(0, 0, 0, 0);
	glContext.clear(glContext.COLOR_BUFFER_BIT);

	const charRenderer = new CharacterRenderer(glContext);
	charRenderer.uploadAtlas(atlas);

	renderCharacterStrip(charRenderer, characterData, 0, sheetWidth, sheetHeight);

	const result = document.createElement('canvas');
	result.width = sheetWidth;
	result.height = sheetHeight;
	const context = result.getContext('2d');
	if (!context) {
		throw new Error('Failed to create 2D context for player sprite');
	}

	context.drawImage(offscreen, 0, 0);

	return {
		canvas: result,
		columns,
		spriteSize: SPRITE_SIZE,
		count: 1,
		framesPerDirection: WALK_FRAMES,
		directions: DIRECTIONS,
	};
}

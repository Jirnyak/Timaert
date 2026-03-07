/**
 * Subworld type definitions.
 *
 * A subworld is a self-contained "game in game" the player enters
 * when visiting settlements or local areas.  It uses free-form
 * (non-grid) movement, its own tick system, and a Canvas2D renderer
 * so it is fully decoupled from the main WebGL world.
 */

import type {CitizenSpriteSheet} from './citizen-sprites';

// ── Geometry primitives ─────────────────────────────────────────

export type Vec2 = {x: number; y: number};

export type AABB = {
	x: number;
	y: number;
	w: number;
	h: number;
};

// ── Entity layer ────────────────────────────────────────────────

export type EntityKind =
	| 'player'
	| 'npc'
	| 'building'
	| 'prop'
	| 'zone';

export type ZoneAction =
	| {type: 'exit'}
	| {type: 'trade'}
	| {type: 'rest'; cost: number}
	| {type: 'dialog'; text: string};

export type SubworldEntity = {
	id: number;
	kind: EntityKind;
	/** World position (pixels, continuous). */
	x: number;
	y: number;
	/** Velocity (pixels / s). */
	vx: number;
	vy: number;
	/** Visual radius (half-size for collision & draw). */
	radius: number;
	/** True = blocks movement of other entities. */
	solid: boolean;
	/** Display label (NPC name, building name, etc.). */
	label: string;
	/** Fill color for placeholder rendering. */
	color: string;
	/** For zone entities — what happens on overlap. */
	action?: ZoneAction;
	/** Sprite key (future: atlas lookup). */
	sprite?: string;
	/** Index into pre-rendered citizen sprite sheet. */
	spriteIndex?: number;
	/** Health (optional, for NPCs). */
	hp?: number;
	maxHp?: number;
	/** Simple AI tag. */
	ai?: 'wander' | 'idle' | 'patrol';
	/** AI internal timer (seconds). */
	aiTimer?: number;
	/** Walk animation frame index (0–5). */
	animFrame?: number;
	/** Walk animation timer (seconds). */
	animTimer?: number;
};

// ── Grid-based collision data ───────────────────────────────────

export type TraversabilityGrid = {
	/** Grid width in tiles. */
	width: number;
	/** Grid height in tiles. */
	height: number;
	/** Per-tile walkability: 0 = blocked, 255 = walkable. */
	data: Uint8Array;
};

// ── Subworld definition ─────────────────────────────────────────

export type SubworldConfig = {
	/** Unique seed for procedural content. */
	seed: number;
	/** World size in tiles (grid units). */
	width: number;
	height: number;
	/** Background fill color (fallback). */
	bgColor: string;
	/** All entities (including the player entity at index 0). */
	entities: SubworldEntity[];
	/** Ground tile color for a simple checkerboard (fallback). */
	groundColorA: string;
	groundColorB: string;
	/** Human-readable name shown in the HUD. */
	name: string;
	/** Pre-rendered map image from CityGenerator. */
	bgImage?: HTMLCanvasElement;
	/** Grid-based traversability for collision. */
	traversability?: TraversabilityGrid;
	/** Pixels per tile for coordinate mapping (default 1). */
	scale: number;
	/** Pre-rendered citizen sprite sheet for NPC rendering. */
	citizenSheet?: CitizenSpriteSheet;
	/** Pre-rendered player character sprite sheet (animated). */
	playerSheet?: CitizenSpriteSheet;
};

// ── Interaction results passed back to main game ────────────────

export type SubworldResult =
	| {type: 'exit'}
	| {type: 'trade'; settlementId: number}
	| {type: 'rest'; cost: number};

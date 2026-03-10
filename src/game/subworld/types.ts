/**
 * Subworld type definitions.
 *
 * A subworld is a self-contained 'game in game' the player enters
 * when visiting settlements, exploring wilderness, or fighting battles.
 * Uses free-form movement, its own tick system, and Canvas2D renderer
 * — fully decoupled from the main WebGL world.
 */

import type {ArmyComposition} from '../army';
import type {CitizenSpriteSheet} from './citizen-sprites';

// ── Geometry ────────────────────────────────────────────────────

export type Vec2 = {x: number; y: number};

// ── Entity model ────────────────────────────────────────────────

export type EntityKind =
	| 'player'
	| 'npc'
	| 'building'
	| 'prop'
	| 'zone';

export type AiKind = 'wander' | 'idle' | 'patrol' | 'combat' | 'flee';

export type ZoneAction =
	| {type: 'exit'}
	| {type: 'trade'}
	| {type: 'rest'; cost: number}
	| {type: 'dialog'; text: string};

export type SubworldEntity = {
	id: number;
	kind: EntityKind;
	x: number;
	y: number;
	vx: number;
	vy: number;
	radius: number;
	solid: boolean;
	label: string;
	color: string;
	// Zone trigger
	action?: ZoneAction;
	// Rendering
	sprite?: string;
	spriteIndex?: number;
	// Combat (per-entity stats — derived from templates at spawn time)
	hp?: number;
	maxHp?: number;
	team?: number;
	unitType?: number;
	attackTimer?: number;
	damage?: number;
	speed?: number;
	attackRange?: number;
	cooldown?: number;
	// Identity
	factionId?: string;
	npcType?: number;
	// AI
	ai?: AiKind;
	aiTimer?: number;
	// Animation
	animFrame?: number;
	animTimer?: number;
	/** Hit-flash countdown (seconds). Entity flashes red while > 0. */
	hitTimer?: number;
};

// ── Grid collision ──────────────────────────────────────────────

export type TraversabilityGrid = {
	width: number;
	height: number;
	data: Uint8Array;
};

// ── Scene descriptor ────────────────────────────────────────────

export type SubworldConfig = {
	seed: number;
	width: number;
	height: number;
	bgColor: string;
	groundColorA: string;
	groundColorB: string;
	entities: SubworldEntity[];
	name: string;
	bgImage?: HTMLCanvasElement;
	traversability?: TraversabilityGrid;
	scale: number;
	citizenSheet?: CitizenSpriteSheet;
	playerSheet?: CitizenSpriteSheet;
	/** Player melee damage. */
	playerDamage?: number;
	/** Player melee attack range (grid units). */
	playerRange?: number;
	/** Player attack cooldown (seconds). */
	playerCooldown?: number;
	/** Faction relations from macroworld diplomacy. */
	factions?: Record<string, {relations: Record<string, number>}>;
	/** Player reputation per faction (from macroworld). */
	playerReputation?: Record<string, number>;
};

// ── Fight context (ARPG army battle) ────────────────────────────

/** Passed when player initiates a fight from the macroworld interaction overlay. */
export type FightContext = {
	playerArmy: ArmyComposition;
	enemyArmy: ArmyComposition;
	enemyFactionId: string;
	enemyName: string;
};

/** Changes accumulated during subworld session, propagated to macroworld on exit. */
export type SubworldResult = {
	/** Cumulative reputation changes per faction (negative = hostility gained). */
	relationChanges: Record<string, number>;
	/** Player HP when leaving. */
	playerHp: number;
	/** Surviving player army (only set when fight context was active). */
	playerArmySurvivors?: ArmyComposition;
	/** Surviving enemy army (only set when fight context was active). */
	enemyArmySurvivors?: ArmyComposition;
};

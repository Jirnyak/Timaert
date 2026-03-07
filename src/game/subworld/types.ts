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
	| 'soldier'
	| 'building'
	| 'prop'
	| 'zone';

export type AiKind = 'wander' | 'idle' | 'patrol' | 'combat';

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
	// Combat
	hp?: number;
	maxHp?: number;
	team?: number;
	unitType?: number;
	attackTimer?: number;
	// AI
	ai?: AiKind;
	aiTimer?: number;
	// Animation
	animFrame?: number;
	animTimer?: number;
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
	/** Player melee damage (used when player attacks soldiers). */
	playerDamage?: number;
};

// ── Battle data ─────────────────────────────────────────────────

export type BattleSubworldOptions = {
	seed: number;
	playerArmy: ArmyComposition;
	enemyArmy: ArmyComposition;
	enemyName: string;
	enemyNpcId: number;
	playerHp: number;
	playerMaxHp: number;
	playerDamage: number;
};

export type BattleResult = {
	victory: boolean;
	survivingArmy: ArmyComposition;
	enemySurviving: ArmyComposition;
	playerHp: number;
};

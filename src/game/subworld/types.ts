/**
 * Subworld type definitions.
 *
 * A subworld is a self-contained 'game in game' the player enters
 * when visiting settlements, exploring wilderness, or fighting battles.
 * Uses free-form movement, its own tick system, and Canvas2D renderer
 * — fully decoupled from the main WebGL world.
 */

import type {ArmyComposition} from '../army';
import type {Item} from '../items';
import type {CitizenSpriteSheet} from './citizen-sprites';
import type {Structure} from './map-data';

// ── Geometry ────────────────────────────────────────────────────

export type Vec2 = {x: number; y: number};
export type Vec3 = {x: number; y: number; z: number};

// ── Entity model ────────────────────────────────────────────────

export type EntityKind =
	| 'player'
	| 'npc'
	| 'building'
	| 'prop'
	| 'zone'
	| 'projectile';

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
	/** Height above terrain (z-axis). 0 = on ground. */
	z: number;
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
	/** Universal attack kind: 'melee' (instant in range) or 'missile' (spawns projectile). */
	attackKind?: 'melee' | 'missile';
	/** Missile speed (px/s), used when attackKind === 'missile'. */
	missileSpeed?: number;
	/** Missile blast radius (0 = single target). */
	missileBlast?: number;
	/** Missile visual color. */
	missileColor?: string;
	// Identity
	factionId?: string;
	npcType?: number;
	/** RPG level — used for XP and loot scaling on death. */
	level?: number;
	/** Id of last entity that damaged this one — drives kill credit. */
	lastAttackerId?: number;
	/**
	 * Per-entity hostility flag toward the player, set when the player
	 * attacks a non-allied non-hostile NPC. Lives only in the subworld
	 * session — never serialised back to macroworld.
	 */
	tempHostileToPlayer?: boolean;
	// AI
	ai?: AiKind;
	aiTimer?: number;
	// Animation
	animFrame?: number;
	animTimer?: number;
	/** Hit-flash countdown (seconds). Entity flashes red while > 0. */
	hitTimer?: number;
	// Projectile fields
	/** Remaining lifetime in seconds. */
	lifeTimer?: number;
	/** Owner entity id (who cast it). */
	ownerId?: number;
	/** Owner faction id — projectile is hostile only to entities the owner is hostile to. */
	ownerFactionId?: string;
	/** Explosion radius (0 = single-target). */
	blastRadius?: number;
	/** Whether this projectile hits friendlies. */
	friendlyFire?: boolean;
	/** Spell id for visual rendering. */
	spellId?: string;
	/** World-space origin X (beam rendering). */
	originX?: number;
	/** World-space origin Y (beam rendering). */
	originY?: number;
	/** Initial life timer (for progress calculations). */
	maxLifeTimer?: number;
	/** Visual-only entity — skip physics, only decay lifetime. */
	visualOnly?: boolean;
	/** Apply blast/beam damage when lifeTimer expires. */
	explodeOnExpiry?: boolean;
	/** Vertical velocity for jumping/falling. */
	vz?: number;
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
	bgImage?: HTMLCanvasElement | ImageBitmap;
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
	/** Heightmap — terrain elevation per cell (0.0–1.0). */
	heightmap?: Float32Array;
	/** 3D structures for raycaster rendering. */
	structures?: Structure[];
	/** Tile grid — per-cell tile type (0–7) for terrain material lookup. */
	tileGrid?: Uint8Array;
	/**
	 * Optional immediate-loot sink. When provided, kill rewards (gold + items)
	 * are delivered as they happen instead of being buffered until exit.
	 * Used by the UI layer to push loot straight into the player inventory so
	 * it shows up in the (now-shareable) inventory overlay during the session.
	 */
	onLoot?: (gold: number, items: Item[]) => void;
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
	/** Total XP awarded to the player from kills in this session. */
	expGained?: number;
	/** Total gold dropped from kills in this session. */
	lootGold?: number;
	/** Item drops from kills, ready to be merged into player inventory. */
	lootItems?: Item[];
	/** Player MP when leaving. */
	playerMp: number;
	/** Player SP when leaving. */
	playerSp: number;
	/** Surviving player army (only set when fight context was active). */
	playerArmySurvivors?: ArmyComposition;
	/** Surviving enemy army (only set when fight context was active). */
	enemyArmySurvivors?: ArmyComposition;
	/** NPC deaths per faction accumulated during the session. */
	npcDeaths?: Record<string, number>;
};

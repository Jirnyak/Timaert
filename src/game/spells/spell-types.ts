// === Spell System — Type Definitions ===
// Pure data types. No logic, no imports from engine layers.

/** Element tags — not schools, just flavor for AI scoring and item synergy. */
export type SpellTag = 'fire' | 'ice' | 'lightning' | 'dark' | 'light' | 'earth' | 'air' | 'arcane' | 'body' | 'mind';

/** Context passed to spell render functions. */
export type SpellRenderContext = {
	ctx: CanvasRenderingContext2D;
	/** Screen position X. */
	sx: number;
	/** Screen position Y. */
	sy: number;
	/** Screen-space radius. */
	sr: number;
	/** World velocity X. */
	vx: number;
	/** World velocity Y. */
	vy: number;
	/** Remaining lifetime (seconds). */
	lifeTimer: number;
	/** Initial lifetime (seconds). */
	maxLifeTimer: number;
	/** World-space origin X (for beams). */
	originX: number;
	/** World-space origin Y (for beams). */
	originY: number;
	/** Camera offset X. */
	ox: number;
	/** Camera offset Y. */
	oy: number;
	/** Pixels-per-tile scale. */
	scale: number;
	/** Global animation time (seconds). */
	time: number;
};

/** Context passed to spell aura render functions (self/sustained spells). */
export type SpellAuraContext = {
	ctx: CanvasRenderingContext2D;
	/** Screen position X of caster. */
	sx: number;
	/** Screen position Y of caster. */
	sy: number;
	/** Screen-space radius of caster. */
	radius: number;
	/** Global animation time (seconds). */
	time: number;
};

/** Context passed to a spell's spawn function. */
export type SpellSpawnContext = {
	/** Player world X. */
	px: number;
	/** Player world Y. */
	py: number;
	/** Player radius. */
	playerRadius: number;
	/** Player entity id. */
	playerId: number;
	/** Direction X (normalized). */
	nx: number;
	/** Direction Y (normalized). */
	ny: number;
	/** Computed damage. */
	damage: number;
	/** Base projectile speed (px/s). */
	speed: number;
	/** Projectile visual radius. */
	radius: number;
	/** Blast radius (AoE). */
	blastRadius: number;
	/** Friendly fire flag. */
	friendlyFire: boolean;
	/** Spell color (tag-based). */
	color: string;
	/** Spell id. */
	spellId: string;
	/** Engine RNG (0–1). */
	rng: () => number;
};

/** A partial entity descriptor returned by spell spawn functions. */
export type SpellProjectileDescriptor = {
	x: number;
	y: number;
	vx: number;
	vy: number;
	radius: number;
	color: string;
	damage: number;
	lifeTimer: number;
	maxLifeTimer: number;
	blastRadius: number;
	friendlyFire: boolean;
	spellId: string;
	originX?: number;
	originY?: number;
	visualOnly?: boolean;
	/** Apply blast/beam damage when lifeTimer expires (visual-first pattern). */
	explodeOnExpiry?: boolean;
};

/** Beam damage descriptor — instant ray damage from spawn. */
export type SpellBeamDescriptor = {
	ox: number;
	oy: number;
	nx: number;
	ny: number;
	length: number;
	width: number;
	damage: number;
	friendlyFire: boolean;
};

/**
 * Spell effect — render + spawn functions.
 * Each spell exports its own. The engine and renderer call these generically.
 */
export type SpellEffect = {
	/** Draw the projectile / visual entity. Return false to use generic fallback. */
	renderProjectile?: (rc: SpellRenderContext) => void;
	/** Draw aura around caster (sustained / self spells). */
	renderAura?: (ac: SpellAuraContext) => void;
	/**
	 * Spawn projectile entities. Returns array of descriptors.
	 * If undefined, engine uses default single-projectile spawn.
	 */
	spawn?: (sc: SpellSpawnContext) => SpellProjectileDescriptor[];
	/**
	 * Instant beam damage on cast (e.g. energy beam).
	 * If defined, engine applies ray damage on cast.
	 */
	beamDamage?: (sc: SpellSpawnContext) => SpellBeamDescriptor;
};

/** Delivery shape in microworld (2D ARPG). */
export type DeliveryShape =
	| 'projectile' // Flies forward, hits first target (or explodes)
	| 'beam' // Instant line pierce
	| 'nova' // Ring expanding from caster
	| 'self' // Buff / heal on caster
	| 'aura' // Sustained area around caster
	| 'chain' // Jumps between targets
	| 'summon' // Creates entity
	| 'targeted'; // Instant hit on selected target

/** What a spell does in microworld (ARPG combat). */
export type MicroEffect = {
	shape: DeliveryShape;
	baseDamage: number; // 0 for pure utility
	baseHeal: number; // 0 for offensive
	baseRadius: number; // 0 for single-target
	chainCount: number; // 0 unless chain shape
	chainDecay: number; // Damage multiplier per jump (e.g. 0.7)
	speed: number; // Projectile / beam travel px/s (0 = instant)
	duration: number; // Ticks for buffs/auras/summons (0 = instant)
	friendlyFire: boolean;
	/** Status effect applied on hit (empty string = none). */
	statusEffect: string;
	statusDuration: number; // Ticks
};

/** What a spell does on the overworld (macro layer). */
export type MacroEffect = {
	/** Text key consumed by macro-world systems. */
	type: 'travel_speed' | 'ignore_terrain' | 'heal_party' | 'damage_region' | 'reveal_map' | 'buff_army' | 'none';
	power: number; // Generic strength scalar
	duration: number; // World-ticks
};

/** Full spell definition — one module per spell. */
export type Spell = {
	id: string;
	name: string;
	icon: string; // Emoji or sprite key
	tags: SpellTag[];
	tier: 1 | 2 | 3 | 4 | 5;
	rarity: 'common' | 'uncommon' | 'rare' | 'epic' | 'mythic';

	manaCost: number;
	cooldown: number; // Seconds
	castTime: number; // Seconds (0 = instant)

	/** Toggle-spell: stays active and drains mana continuously. */
	sustained: boolean;
	/** MP drained per second while sustained spell is active. */
	manaDrain: number;

	/** Scaling coefficients — multiplied by spellStrength(). */
	scaling: {
		power: number;
		duration: number;
		radius: number;
	};

	micro: MicroEffect | undefined;
	macro: MacroEffect | undefined;

	/** Rendering + spawning functions — each spell packages its own. */
	effect: SpellEffect | undefined;

	/** Short flavour pros list (shown in UI). */
	pros: string[];
	/** Short flavour cons list (shown in UI). */
	cons: string[];
	/** One-paragraph description for the spell book. */
	description: string;
};

/** Runtime state for a spell the player knows. */
export type SpellState = {
	id: string;
	cooldownRemaining: number; // Seconds left
};

/** Player spell data — stored alongside PlayerState. */
export type SpellBook = {
	/** IDs of all learned spells. */
	learned: string[];
	/** ID of currently selected (active) spell, or empty string. */
	activeSpellId: string;
	/** Per-spell runtime cooldown tracking. */
	cooldowns: Record<string, number>;
	/** IDs of currently active sustained spells. */
	sustainedActive: string[];
};

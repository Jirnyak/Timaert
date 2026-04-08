/**
 * Subworld simulation engine — Action RPG.
 *
 * One unified engine for all subworld modes: settlement, nature.
 * All entities share the same combat system: any entity with hp can
 * fight. Hostility is driven by faction reputation — attacking a
 * friendly-faction NPC costs −1 reputation per hit. Once reputation
 * drops below −50 the entire faction turns hostile and attacks back.
 *
 * Every non-player entity is kind='npc' — there is no separate
 * 'soldier' type. Army units are NPCs with unitType + factionId.
 *
 * Reference: Might & Magic VI — Mandate of Heaven (ARPG mode).
 */

import {
	type UnitType, getDamageMultiplier, countSurvivors,
} from '../army';
import {xorshift32} from '../rng';
import {SPELL_CATALOG} from '../spells/index';
import type {SpellSpawnContext} from '../spells/spell-types';
import type {
	SubworldConfig,
	SubworldEntity,
	SubworldResult,
	TraversabilityGrid,
	Vec2,
	ZoneAction,
} from './types';
import {tickWander, tickCombatMove, tickFlee} from './ai';

// ── Constants ───────────────────────────────────────────────────

const WALK_FRAME_DURATION = 0.125;
const WALK_FRAME_COUNT = 6;

/** Reputation below which a faction becomes hostile to the player. */
const HOSTILE_THRESHOLD = -50;
/** Reputation lost per hit on a friendly-faction entity. */
const HIT_REP_PENALTY = -1;
/** Duration of the red hit-flash overlay (seconds). */
const HIT_FLASH_DURATION = 0.15;
/**
 * Distance penalty per additional attacker already targeting an enemy.
 * Spreads units across multiple targets for organic battles.
 */
const CROWD_PENALTY = 40;

// ── Exported helpers ────────────────────────────────────────────

export function tileWalkable(grid: TraversabilityGrid, x: number, y: number): boolean {
	const gx = Math.floor(x);
	const gy = Math.floor(y);
	if (gx < 0 || gx >= grid.width || gy < 0 || gy >= grid.height) {
		return false;
	}

	return grid.data[gy * grid.width + gx] > 0;
}

/** Find a random walkable position near (cx, cy) within searchRadius. */
export function findWalkable(
	grid: TraversabilityGrid,
	rng: () => number,
	cx: number,
	cy: number,
	searchRadius: number,
): Vec2 | undefined {
	for (let attempt = 0; attempt < 80; attempt++) {
		const angle = rng() * Math.PI * 2;
		const distance = rng() * searchRadius;
		const tx = Math.floor(cx + Math.cos(angle) * distance);
		const ty = Math.floor(cy + Math.sin(angle) * distance);
		if (tileWalkable(grid, tx, ty)) {
			return {x: tx, y: ty};
		}
	}

	return undefined;
}

/** Build a SubworldEntity with sensible defaults. */
export function makeEntity(
	nextId: {value: number},
	partial: Partial<SubworldEntity> & {kind: SubworldEntity['kind']},
): SubworldEntity {
	const id = nextId.value++;
	return {
		id,
		x: 0,
		y: 0,
		z: 0,
		vx: 0,
		vy: 0,
		radius: 4,
		solid: false,
		label: '',
		color: '#888',
		...partial,
	};
}

// ── Private helpers ─────────────────────────────────────────────

function clamp(value: number, lo: number, hi: number): number {
	return Math.max(lo, Math.min(hi, value));
}

function circleOverlap(
	ax: number, ay: number, ar: number,
	bx: number, by: number, br: number,
): boolean {
	const dx = ax - bx;
	const dy = ay - by;
	const rr = ar + br;
	return (dx * dx + dy * dy) < (rr * rr);
}

function canOccupy(grid: TraversabilityGrid, cx: number, cy: number, r: number): boolean {
	return tileWalkable(grid, cx - r, cy - r)
		&& tileWalkable(grid, cx + r, cy - r)
		&& tileWalkable(grid, cx - r, cy + r)
		&& tileWalkable(grid, cx + r, cy + r)
		&& tileWalkable(grid, cx, cy);
}

/** Entity combat stat accessors — stats are baked in at spawn time. */
function entitySpeed(entity: SubworldEntity): number {
	return entity.speed ?? 40;
}

function entityRange(entity: SubworldEntity): number {
	return entity.attackRange ?? 3;
}

function entityDamage(entity: SubworldEntity): number {
	return entity.damage ?? 10;
}

function entityCooldown(entity: SubworldEntity): number {
	return entity.cooldown ?? 1;
}

// ── Engine ──────────────────────────────────────────────────────

export class SubworldEngine {
	entities: SubworldEntity[];
	player: SubworldEntity;
	rng: () => number;

	/** Input direction (raw, not normalized). Set by the screen. */
	inputDir: Vec2 = {x: 0, y: 0};

	/** Whether the player is holding the attack key. */
	attackHeld = false;

	/** Whether the player is currently flying (ignores terrain). */
	playerFlying = false;

	/** The most recently triggered zone action (consumed by screen). */
	pendingAction: ZoneAction | undefined;

	/** Player movement speed (grid tiles / s). */
	playerSpeed = 80;

	/** NPC wander speed (grid tiles / s). */
	npcSpeed = 20;

	/** Locked combat targets: attacking entity id → target entity id. */
	private readonly targetMap = new Map<number, number>();

	/** Fast entity lookup by ID — rebuilt when entities change. */
	private readonly entityById = new Map<number, SubworldEntity>();

	/** Pre-computed crowd counts per tick (target id → attacker count). */
	private readonly tickCrowd = new Map<number, number>();

	/** Fight faction IDs for army survivor counting. */
	private playerArmyFaction?: string;
	private enemyArmyFaction?: string;

	/**
	 * Mutable reputation snapshot — starts from macroworld values,
	 * modified if the player attacks friendlies.
	 */
	reputation: Record<string, number>;

	/** Accumulated relation deltas to propagate back to macroworld. */
	relationChanges: Record<string, number> = {};

	/** NPC deaths accumulated per faction. */
	npcDeaths: Record<string, number> = {};

	private nextId: number;

	constructor(readonly config: SubworldConfig) {
		this.entities = config.entities;
		this.player = this.entities.find(entity => entity.kind === 'player')!;
		this.rng = xorshift32(config.seed);
		this.reputation = {...config.playerReputation};

		let maxId = 0;
		for (const entity of this.entities) {
			if (entity.id > maxId) {
				maxId = entity.id;
			}

			this.entityById.set(entity.id, entity);
		}

		this.nextId = maxId + 1;
	}

	private rebuildEntityById(): void {
		this.entityById.clear();
		for (const entity of this.entities) {
			this.entityById.set(entity.id, entity);
		}
	}

	// ── Faction helpers ───────────────────────────────────────

	/** Is entity hostile to the player? */
	isHostileToPlayer(entity: SubworldEntity): boolean {
		const faction = entity.factionId;
		if (!faction) {
			return false;
		}

		// Cults are always hostile
		if (faction === 'cults') {
			return true;
		}

		const rep = this.reputation[faction] ?? 0;
		return rep < HOSTILE_THRESHOLD;
	}

	/** Are two non-player entities hostile to each other? */
	private areHostile(a: SubworldEntity, b: SubworldEntity): boolean {
		const fA = a.factionId;
		const fB = b.factionId;
		if (!fA || !fB || fA === fB) {
			return false;
		}

		const {factions} = this.config;
		if (!factions) {
			return false;
		}

		const rel = factions[fA]?.relations[fB] ?? 0;
		return rel < HOSTILE_THRESHOLD;
	}

	/** Gradual aggression: each hit on a friendly entity costs −1 rep. */
	private playerAttackedFaction(faction: string): void {
		if (!faction) {
			return;
		}

		this.reputation[faction] = (this.reputation[faction] ?? 0) + HIT_REP_PENALTY;
		this.relationChanges[faction]
			= (this.relationChanges[faction] ?? 0) + HIT_REP_PENALTY;
	}

	// ── Public API ────────────────────────────────────────────

	tick(dt: number): void {
		this.movePlayer(dt);
		this.playerMelee(dt);
		this.updateProjectiles(dt);
		this.updateNpcs(dt);
		this.resolveCombat(dt);
		this.updateAnimations(dt);
		this.decayHitTimers(dt);
		this.resolveCollisions();
		this.checkZones();
		this.reapDead();
	}

	consumeAction(): ZoneAction | undefined {
		const action = this.pendingAction;
		this.pendingAction = undefined;
		return action;
	}

	/** Snapshot changes to propagate back to macroworld on exit. */
	getResult(): SubworldResult {
		const result: SubworldResult = {
			relationChanges: {...this.relationChanges},
			playerHp: this.player.hp ?? 0,
		};

		// Count surviving army units if a fight was active
		if (this.playerArmyFaction) {
			result.playerArmySurvivors = countSurvivors(this.entities, this.playerArmyFaction);
		}

		if (this.enemyArmyFaction) {
			result.enemyArmySurvivors = countSurvivors(this.entities, this.enemyArmyFaction);
		}

		// Include NPC death counts if any occurred
		if (Object.keys(this.npcDeaths).length > 0) {
			result.npcDeaths = {...this.npcDeaths};
		}

		return result;
	}

	/** Register army factions so getResult can count survivors. */
	setFightFactions(playerFaction: string, enemyFaction: string): void {
		this.playerArmyFaction = playerFaction;
		this.enemyArmyFaction = enemyFaction;
	}

	addEntity(partial: Partial<SubworldEntity> & {kind: SubworldEntity['kind']}): SubworldEntity {
		const id = this.nextId++;
		const entity: SubworldEntity = {
			id,
			x: 0,
			y: 0,
			z: 0,
			vx: 0,
			vy: 0,
			radius: 4,
			solid: false,
			label: '',
			color: '#888',
			...partial,
		};
		this.entities.push(entity);
		return entity;
	}

	// ── Movement ──────────────────────────────────────────────

	private movePlayer(dt: number): void {
		const {inputDir} = this;
		const length = Math.sqrt(inputDir.x * inputDir.x + inputDir.y * inputDir.y);
		if (length > 0.01) {
			const nx = inputDir.x / length;
			const ny = inputDir.y / length;
			this.player.vx = nx * this.playerSpeed;
			this.player.vy = ny * this.playerSpeed;
		} else {
			this.player.vx = 0;
			this.player.vy = 0;
		}

		const newX = this.player.x + this.player.vx * dt;
		const newY = this.player.y + this.player.vy * dt;
		const r = this.player.radius;

		const grid = this.config.traversability;
		if (grid && !this.playerFlying) {
			if (canOccupy(grid, newX, this.player.y, r)) {
				this.player.x = newX;
			}

			if (canOccupy(grid, this.player.x, newY, r)) {
				this.player.y = newY;
			}
		} else {
			this.player.x = newX;
			this.player.y = newY;
		}

		this.player.x = clamp(this.player.x, r, this.config.width - r);
		this.player.y = clamp(this.player.y, r, this.config.height - r);
	}

	/**
	 * Player melee — manual attack (A key held).
	 * Damages the nearest entity with hp within range.
	 * If the target belongs to a friendly faction, reputation drops by 1.
	 */
	private playerMelee(dt: number): void {
		if ((this.player.hp ?? 0) <= 0) {
			return;
		}

		this.player.attackTimer = (this.player.attackTimer ?? 0) - dt;

		if (!this.attackHeld || this.player.attackTimer > 0) {
			return;
		}

		const range = this.config.playerRange ?? 5;
		let nearest: SubworldEntity | undefined;
		let bestDist = Infinity;

		for (const entity of this.entities) {
			if (entity === this.player) {
				continue;
			}

			if (entity.kind !== 'npc' || (entity.hp ?? 0) <= 0) {
				continue;
			}

			const dx = entity.x - this.player.x;
			const dy = entity.y - this.player.y;
			const d = Math.sqrt(dx * dx + dy * dy);
			if (d < bestDist) {
				bestDist = d;
				nearest = entity;
			}
		}

		if (!nearest || bestDist > range) {
			return;
		}

		const damage = this.config.playerDamage ?? 10;
		nearest.hp = (nearest.hp ?? 0) - damage;
		nearest.hitTimer = HIT_FLASH_DURATION;
		this.player.attackTimer = this.config.playerCooldown ?? 0.5;

		// Gradual faction aggression: −1 rep per hit on a non-hostile entity
		if (nearest.factionId && !this.isHostileToPlayer(nearest)) {
			this.playerAttackedFaction(nearest.factionId);
		}
	}

	/**
	 * Unified NPC update — single pass over all NPCs.
	 *
	 * Decision logic per entity:
	 *  1. ai === 'combat' → always chase nearest enemy (army units)
	 *  2. hostile to player → chase nearest enemy (provoked citizens)
	 *  3. has an enemy from another faction nearby → chase it
	 *  4. ai === 'wander' → random walk (peaceful citizens)
	 *  5. otherwise → idle
	 */
	private updateNpcs(dt: number): void {
		// Build crowd map once per tick for acquireTarget
		this.tickCrowd.clear();
		for (const [, tid] of this.targetMap) {
			this.tickCrowd.set(tid, (this.tickCrowd.get(tid) ?? 0) + 1);
		}

		for (const entity of this.entities) {
			if (entity === this.player || entity.kind !== 'npc') {
				continue;
			}

			if ((entity.hp ?? 0) <= 0) {
				continue;
			}

			// Flee AI — citizens run from nearest hostile, wander otherwise
			if (entity.ai === 'flee') {
				const [threat, dist] = this.findNearestHostile(entity);
				tickFlee(entity, dt, this.config.traversability, this.config.width, this.config.height, this.npcSpeed, this.rng, threat, dist);
				continue;
			}

			// Combat: explicit combat-AI (guards/army) or hostile non-flee NPCs
			const isCombatAi = entity.ai === 'combat';
			const isHostile = entity.hp !== undefined && this.isHostileToPlayer(entity);

			if (isCombatAi || isHostile) {
				const target = this.acquireTarget(entity);
				if (target) {
					tickCombatMove(entity, target, entitySpeed(entity), entityRange(entity), dt, this.config.width, this.config.height);
					continue;
				}

				// No enemies left — pure combat-AI stops, wander NPCs fall through
				if (isCombatAi) {
					entity.vx = 0;
					entity.vy = 0;
					continue;
				}
			}

			// Peaceful wander (or hostile wander with no targets left)
			if (entity.ai === 'wander') {
				tickWander(entity, dt, this.config.traversability, this.config.width, this.config.height, this.npcSpeed, this.rng);
			}
		}
	}

	/**
	 * Find the nearest entity hostile to `source` using faction relations.
	 * Returns [entity, distance] or [undefined, Infinity].
	 */
	private findNearestHostile(source: SubworldEntity): [SubworldEntity | undefined, number] {
		let nearest: SubworldEntity | undefined;
		let bestDistSq = 90_000; // 300² — search radius limit

		const sx = source.x;
		const sy = source.y;

		for (const other of this.entities) {
			if (other === source) {
				continue;
			}

			if ((other.hp ?? 0) <= 0) {
				continue;
			}

			if (other.kind !== 'npc' && other.kind !== 'player') {
				continue;
			}

			// Quick distance² check before expensive faction test
			const dx = other.x - sx;
			const dy = other.y - sy;
			const distSq = dx * dx + dy * dy;
			if (distSq >= bestDistSq) {
				continue;
			}

			if (!this.entitiesHostile(source, other)) {
				continue;
			}

			bestDistSq = distSq;
			nearest = other;
		}

		return [nearest, Math.sqrt(bestDistSq)];
	}

	private updateAnimations(dt: number): void {
		for (const entity of this.entities) {
			if (entity.kind !== 'player' && entity.kind !== 'npc') {
				continue;
			}

			const moving = Math.abs(entity.vx) > 0.1
				|| Math.abs(entity.vy) > 0.1;

			if (moving) {
				entity.animTimer = (entity.animTimer ?? 0) + dt;
				if (entity.animTimer >= WALK_FRAME_DURATION) {
					entity.animTimer -= WALK_FRAME_DURATION;
					entity.animFrame = ((entity.animFrame ?? 0) + 1) % WALK_FRAME_COUNT;
				}
			} else {
				entity.animFrame = 0;
				entity.animTimer = 0;
			}
		}
	}

	// ── Collision & zones ─────────────────────────────────────

	private resolveCollisions(): void {
		for (const solid of this.entities) {
			if (!solid.solid || solid === this.player || solid.kind === 'building') {
				continue;
			}

			// Flying players pass through props/walls but still collide with NPCs
			if (this.playerFlying && solid.kind !== 'npc') {
				continue;
			}

			if (!circleOverlap(this.player.x, this.player.y, this.player.radius, solid.x, solid.y, solid.radius)) {
				continue;
			}

			const dx = this.player.x - solid.x;
			const dy = this.player.y - solid.y;
			const dist = Math.sqrt(dx * dx + dy * dy) || 0.01;
			const overlap = (this.player.radius + solid.radius) - dist;
			if (overlap > 0) {
				this.player.x += (dx / dist) * overlap;
				this.player.y += (dy / dist) * overlap;
			}
		}

		const r = this.player.radius;
		this.player.x = clamp(this.player.x, r, this.config.width - r);
		this.player.y = clamp(this.player.y, r, this.config.height - r);
	}

	private checkZones(): void {
		for (const entity of this.entities) {
			if (entity.kind !== 'zone' || !entity.action) {
				continue;
			}

			if (circleOverlap(this.player.x, this.player.y, this.player.radius, entity.x, entity.y, entity.radius)) {
				this.pendingAction = entity.action;
				break;
			}
		}
	}

	// ── Combat ─────────────────────────────────────────────────

	/** Decay hit-flash timers on all entities. */
	private decayHitTimers(dt: number): void {
		for (const entity of this.entities) {
			if (entity.hitTimer && entity.hitTimer > 0) {
				entity.hitTimer -= dt;
			}
		}
	}

	/**
	 * Hostile NPCs attack their locked target when in range.
	 * Sets hitTimer on the target for visual feedback.
	 */
	private resolveCombat(dt: number): void {
		for (const attacker of this.entities) {
			if (attacker === this.player) {
				continue;
			}

			if ((attacker.hp ?? 0) <= 0) {
				continue;
			}

			if (attacker.kind !== 'npc') {
				continue;
			}

			// Flee-AI citizens never attack
			if (attacker.ai === 'flee') {
				continue;
			}

			const target = this.acquireTarget(attacker);
			if (!target) {
				continue;
			}

			attacker.attackTimer = (attacker.attackTimer ?? 0) - dt;
			if (attacker.attackTimer > 0) {
				continue;
			}

			const range = entityRange(attacker);
			const dx = target.x - attacker.x;
			const dy = target.y - attacker.y;
			const dist = Math.sqrt(dx * dx + dy * dy);
			if (dist > range) {
				continue;
			}

			let dmg = entityDamage(attacker);
			const defenderType = target.unitType as UnitType | undefined;
			const attackerType = attacker.unitType as UnitType | undefined;
			if (attackerType !== undefined && defenderType !== undefined) {
				dmg *= getDamageMultiplier(attackerType, defenderType);
			}

			target.hp = (target.hp ?? 0) - dmg;
			target.hitTimer = HIT_FLASH_DURATION;
			attacker.attackTimer = entityCooldown(attacker);
		}
	}

	// ── Projectile simulation ─────────────────────────────────

	private updateProjectiles(dt: number): void {
		for (let i = this.entities.length - 1; i >= 0; i--) {
			const p = this.entities[i];
			if (p.kind !== 'projectile') {
				continue;
			}

			// Advance lifetime
			p.lifeTimer = (p.lifeTimer ?? 0) - dt;
			if (p.lifeTimer <= 0) {
				if (p.explodeOnExpiry) {
					this.deferredDamage(p);
				}

				this.entities.splice(i, 1);
				continue;
			}

			// Visual-only entities skip physics
			if (p.visualOnly) {
				continue;
			}

			// Move
			p.x += p.vx * dt;
			p.y += p.vy * dt;

			// Out of bounds → remove
			if (p.x < 0 || p.x > this.config.width
				|| p.y < 0 || p.y > this.config.height) {
				this.entities.splice(i, 1);
				continue;
			}

			// Collision with entities
			const hit = this.projectileHitCheck(p);
			if (hit) {
				const blast = p.blastRadius ?? 0;
				if (blast > 0) {
					// AoE explosion
					this.projectileExplode(p, blast);
				} else {
					// Single-target hit
					this.projectileDamage(p, hit);
				}

				this.entities.splice(i, 1);
			}
		}

		// Rebuild lookup only if we actually have projectiles (avoid overhead)
		if (this.entities.some(ent => ent.kind === 'projectile')) {
			this.rebuildEntityById();
		}
	}

	private projectileHitCheck(proj: SubworldEntity): SubworldEntity | undefined {
		const pr = proj.radius;
		for (const ent of this.entities) {
			if (ent.id === proj.ownerId || ent.kind === 'projectile'
				|| ent.kind === 'zone' || ent.kind === 'building' || ent.kind === 'prop') {
				continue;
			}

			if (ent.hp === undefined) {
				continue;
			}

			// Skip friendlies unless friendly fire
			if (!proj.friendlyFire && ent.kind === 'player' && proj.ownerId === this.player.id) {
				continue;
			}

			if (circleOverlap(proj.x, proj.y, pr, ent.x, ent.y, ent.radius)) {
				return ent;
			}
		}

		return undefined;
	}

	private projectileDamage(proj: SubworldEntity, target: SubworldEntity): void {
		const dmg = proj.damage ?? 0;
		if (dmg <= 0) {
			return;
		}

		target.hp = (target.hp ?? 0) - dmg;
		target.hitTimer = HIT_FLASH_DURATION;

		// Reputation penalty if player projectile hits a friendly faction NPC
		if (proj.ownerId === this.player.id && target.factionId
			&& !this.isHostileToPlayer(target)) {
			this.playerAttackedFaction(target.factionId);
		}
	}

	private projectileExplode(proj: SubworldEntity, radius: number): void {
		for (const ent of this.entities) {
			if (ent.kind === 'projectile' || ent.kind === 'zone'
				|| ent.kind === 'building' || ent.kind === 'prop') {
				continue;
			}

			if (ent.hp === undefined) {
				continue;
			}

			// Skip player unless friendly fire
			if (!proj.friendlyFire && ent.id === proj.ownerId) {
				continue;
			}

			const dx = ent.x - proj.x;
			const dy = ent.y - proj.y;
			if (dx * dx + dy * dy <= radius * radius) {
				this.projectileDamage(proj, ent);
			}
		}
	}

	/**
	 * Cast a spell — spawns projectile(s) based on spell type.
	 * Returns true if effect was spawned.
	 */
	castSpell(damage: number, speed: number, radius: number, blastRadius: number, friendlyFire: boolean, tx: number, ty: number, spellColor: string, spellId?: string): boolean {
		if ((this.player.hp ?? 0) <= 0) {
			return false;
		}

		const dx = tx - this.player.x;
		const dy = ty - this.player.y;
		const length = Math.sqrt(dx * dx + dy * dy);
		if (length < 0.1) {
			return false;
		}

		const nx = dx / length;
		const ny = dy / length;
		const projSpeed = speed > 0 ? speed : 300;

		const spell = spellId ? SPELL_CATALOG.get(spellId) : undefined;
		const effect = spell?.effect;

		// ── Spell-defined spawn ─────────────────────────────────
		if (effect?.spawn) {
			const sc: SpellSpawnContext = {
				px: this.player.x,
				py: this.player.y,
				playerRadius: this.player.radius,
				playerId: this.player.id,
				nx, ny,
				damage,
				speed: projSpeed,
				radius,
				blastRadius,
				friendlyFire,
				color: spellColor,
				spellId: spellId ?? '',
				rng: () => this.rng(),
			};

			const descriptors = effect.spawn(sc);
			for (const desc of descriptors) {
				this.addEntity({
					kind: 'projectile',
					x: desc.x,
					y: desc.y,
					vx: desc.vx,
					vy: desc.vy,
					radius: desc.radius,
					solid: false,
					label: '',
					color: desc.color,
					damage: desc.damage,
					lifeTimer: desc.lifeTimer,
					maxLifeTimer: desc.maxLifeTimer,
					ownerId: this.player.id,
					blastRadius: desc.blastRadius,
					friendlyFire: desc.friendlyFire,
					spellId: desc.spellId,
					originX: desc.originX,
					originY: desc.originY,
					visualOnly: desc.visualOnly,
					explodeOnExpiry: desc.explodeOnExpiry,
				});
			}

			this.rebuildEntityById();
			return true;
		}

		// ── Default: single projectile ──────────────────────────
		this.addEntity({
			kind: 'projectile',
			x: this.player.x + nx * (this.player.radius + 2),
			y: this.player.y + ny * (this.player.radius + 2),
			vx: nx * projSpeed,
			vy: ny * projSpeed,
			radius,
			solid: false,
			label: '',
			color: spellColor,
			damage,
			lifeTimer: 3,
			maxLifeTimer: 3,
			ownerId: this.player.id,
			blastRadius,
			friendlyFire,
			spellId,
		});
		this.rebuildEntityById();
		return true;
	}

	/** Apply deferred damage when a visual-first projectile expires. */
	private deferredDamage(p: SubworldEntity): void {
		// AoE blast
		if (p.blastRadius && p.blastRadius > 0 && (p.damage ?? 0) > 0) {
			this.projectileExplode(p, p.blastRadius);
		}

		// Beam damage (energy beam etc.)
		if (!p.spellId || p.originX === undefined || p.originY === undefined) {
			return;
		}

		const bSpell = SPELL_CATALOG.get(p.spellId);
		if (!bSpell?.effect?.beamDamage) {
			return;
		}

		const vLength = Math.sqrt(p.vx * p.vx + p.vy * p.vy);
		const bNx = vLength > 0.001 ? p.vx / vLength : 0;
		const bNy = vLength > 0.001 ? p.vy / vLength : 0;
		const beam = bSpell.effect.beamDamage({
			px: p.originX, py: p.originY,
			playerRadius: this.player.radius,
			playerId: this.player.id,
			nx: bNx, ny: bNy,
			damage: p.damage ?? 0,
			speed: 0, radius: p.radius,
			blastRadius: p.blastRadius ?? 0,
			friendlyFire: p.friendlyFire ?? false,
			color: p.color, spellId: p.spellId,
			rng: () => this.rng(),
		});
		this.beamDamage(beam.ox, beam.oy, beam.nx, beam.ny, beam.length, beam.width, beam.damage, beam.friendlyFire);
	}

	/** Instant beam damage — hits all entities within `width` distance of the ray. */
	private beamDamage(
		ox: number, oy: number,
		nx: number, ny: number,
		beamLength: number, width: number,
		damage: number, friendlyFire: boolean,
	): void {
		for (const ent of this.entities) {
			if (ent.kind === 'projectile' || ent.kind === 'zone'
				|| ent.kind === 'building' || ent.kind === 'prop') {
				continue;
			}

			if (ent.hp === undefined || ent.id === this.player.id) {
				continue;
			}

			if (!friendlyFire && ent.kind === 'player') {
				continue;
			}

			// Project entity center onto beam line
			const dx = ent.x - ox;
			const dy = ent.y - oy;
			const along = dx * nx + dy * ny;
			if (along < 0 || along > beamLength) {
				continue;
			}

			const perpX = dx - nx * along;
			const perpY = dy - ny * along;
			const perpDist = Math.sqrt(perpX * perpX + perpY * perpY);
			if (perpDist <= width + ent.radius) {
				ent.hp = (ent.hp ?? 0) - damage;
				ent.hitTimer = HIT_FLASH_DURATION;
				if (ent.factionId && !this.isHostileToPlayer(ent)) {
					this.playerAttackedFaction(ent.factionId);
				}
			}
		}
	}

	private reapDead(): void {
		let removed = false;
		for (let i = this.entities.length - 1; i >= 0; i--) {
			const entity = this.entities[i];
			if (entity.kind === 'player') {
				continue;
			}

			if (entity.hp !== undefined && entity.hp <= 0) {
				// Track NPC deaths by faction
				if (entity.kind === 'npc' && entity.factionId) {
					this.npcDeaths[entity.factionId]
						= (this.npcDeaths[entity.factionId] ?? 0) + 1;
				}

				this.entities.splice(i, 1);
				removed = true;
				// Release any target locks involving this dead entity
				this.targetMap.delete(entity.id);
				for (const [src, tgt] of this.targetMap) {
					if (tgt === entity.id) {
						this.targetMap.delete(src);
					}
				}
			}
		}

		if (removed) {
			this.rebuildEntityById();
		}
	}

	/**
	 * Acquire a combat target for an AI entity.
	 *
	 * Target-locking: keeps current target while it's alive and hostile.
	 * On re-acquisition, uses a crowd penalty so units spread across
	 * multiple enemies — producing organic team-vs-team battles instead
	 * of everyone piling on the same target.
	 */
	private acquireTarget(source: SubworldEntity): SubworldEntity | undefined {
		// Keep current locked target if still alive and hostile
		const lockedId = this.targetMap.get(source.id);
		if (lockedId !== undefined) {
			const locked = this.entityById.get(lockedId);
			if (locked && (locked.hp ?? 0) > 0 && this.entitiesHostile(source, locked)) {
				return locked;
			}

			this.targetMap.delete(source.id);
		}

		const crowd = this.tickCrowd;

		// Pick best target: distance + crowd penalty
		let best: SubworldEntity | undefined;
		let bestScore = Infinity;

		for (const entity of this.entities) {
			if (entity === source) {
				continue;
			}

			if (entity.kind !== 'npc' && entity.kind !== 'player') {
				continue;
			}

			if ((entity.hp ?? 0) <= 0) {
				continue;
			}

			if (!this.entitiesHostile(source, entity)) {
				continue;
			}

			const dx = entity.x - source.x;
			const dy = entity.y - source.y;
			const dist = Math.sqrt(dx * dx + dy * dy);
			const score = dist + (crowd.get(entity.id) ?? 0) * CROWD_PENALTY;
			if (score < bestScore) {
				bestScore = score;
				best = entity;
			}
		}

		if (best) {
			this.targetMap.set(source.id, best.id);
		}

		return best;
	}

	/** Determine if two entities consider each other enemies. */
	private entitiesHostile(a: SubworldEntity, b: SubworldEntity): boolean {
		// Player or player_army involvement — use player reputation
		// so player's soldiers share exact same hostility as the player.
		const aIsPlayer = a === this.player || a.factionId === 'player_army';
		const bIsPlayer = b === this.player || b.factionId === 'player_army';
		if (aIsPlayer || bIsPlayer) {
			const other = aIsPlayer ? b : a;
			// Never hostile to own side
			if (other === this.player || other.factionId === 'player_army') {
				return false;
			}

			return this.isHostileToPlayer(other);
		}

		return this.areHostile(a, b);
	}
}

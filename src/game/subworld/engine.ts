/**
 * Subworld simulation engine.
 *
 * One unified engine for all subworld modes: settlement, nature, battle.
 * Combat is universal — if soldiers of different teams exist, they fight.
 *
 * The main world simulation is frozen while a subworld is active —
 * this engine owns the entire update cycle.
 */

import {UnitType, UNIT_STATS, getDamageMultiplier} from '../army';
import type {
	SubworldConfig,
	SubworldEntity,
	TraversabilityGrid,
	Vec2,
	ZoneAction,
} from './types';

// ── Constants ───────────────────────────────────────────────────

const WALK_FRAME_DURATION = 0.125;
const WALK_FRAME_COUNT = 6;

// ── Exported helpers ────────────────────────────────────────────

export function seededRng(seed: number): () => number {
	let s = seed;
	return () => {
		s = (s * 1_103_515_245 + 12_345) & 0x7F_FF_FF_FF;
		return s / 0x7F_FF_FF_FF;
	};
}

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

// ── Engine ──────────────────────────────────────────────────────

export class SubworldEngine {
	entities: SubworldEntity[];
	player: SubworldEntity;
	rng: () => number;

	/** Input direction (raw, not normalized). Set by the screen. */
	inputDir: Vec2 = {x: 0, y: 0};

	/** The most recently triggered zone action (consumed by screen). */
	pendingAction: ZoneAction | undefined;

	/** Player movement speed (grid tiles / s). */
	playerSpeed = 80;

	/** NPC wander speed (grid tiles / s). */
	npcSpeed = 20;

	private nextId: number;

	constructor(readonly config: SubworldConfig) {
		this.entities = config.entities;
		this.player = this.entities.find(entity => entity.kind === 'player')!;
		this.rng = seededRng(config.seed);

		let maxId = 0;
		for (const entity of this.entities) {
			if (entity.id > maxId) {
				maxId = entity.id;
			}
		}

		this.nextId = maxId + 1;
	}

	// ── Public API ────────────────────────────────────────────

	tick(dt: number): void {
		this.movePlayer(dt);
		this.updateNpcs(dt);
		this.updateAnimations(dt);
		this.resolveCollisions();
		this.checkZones();

		this.moveSoldiers(dt);
		this.resolveCombat(dt);
		this.reapDead();
	}

	consumeAction(): ZoneAction | undefined {
		const action = this.pendingAction;
		this.pendingAction = undefined;
		return action;
	}

	addEntity(partial: Partial<SubworldEntity> & {kind: SubworldEntity['kind']}): SubworldEntity {
		const id = this.nextId++;
		const entity: SubworldEntity = {
			id,
			x: 0,
			y: 0,
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
		const length = Math.hypot(inputDir.x, inputDir.y);
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
		if (grid) {
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

	private updateNpcs(dt: number): void {
		const grid = this.config.traversability;
		for (const entity of this.entities) {
			if (entity.kind !== 'npc' || !entity.ai) {
				continue;
			}

			if (entity.ai === 'wander') {
				entity.aiTimer = (entity.aiTimer ?? 0) - dt;
				if (entity.aiTimer <= 0) {
					if (this.rng() < 0.4) {
						entity.vx = 0;
						entity.vy = 0;
					} else {
						const angle = this.rng() * Math.PI * 2;
						entity.vx = Math.cos(angle) * this.npcSpeed;
						entity.vy = Math.sin(angle) * this.npcSpeed;
					}

					entity.aiTimer = 1.5 + this.rng() * 3;
				}

				const newX = entity.x + entity.vx * dt;
				const newY = entity.y + entity.vy * dt;
				const r = entity.radius;

				if (grid) {
					if (canOccupy(grid, newX, entity.y, r)) {
						entity.x = newX;
					} else {
						entity.vx = -entity.vx;
					}

					if (canOccupy(grid, entity.x, newY, r)) {
						entity.y = newY;
					} else {
						entity.vy = -entity.vy;
					}
				} else {
					entity.x = newX;
					entity.y = newY;
				}

				if (entity.x < r) {
					entity.x = r;
					entity.vx = Math.abs(entity.vx);
				}

				if (entity.x > this.config.width - r) {
					entity.x = this.config.width - r;
					entity.vx = -Math.abs(entity.vx);
				}

				if (entity.y < r) {
					entity.y = r;
					entity.vy = Math.abs(entity.vy);
				}

				if (entity.y > this.config.height - r) {
					entity.y = this.config.height - r;
					entity.vy = -Math.abs(entity.vy);
				}
			}
		}
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

			if (!circleOverlap(this.player.x, this.player.y, this.player.radius, solid.x, solid.y, solid.radius)) {
				continue;
			}

			const dx = this.player.x - solid.x;
			const dy = this.player.y - solid.y;
			const dist = Math.hypot(dx, dy) || 0.01;
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

	private moveSoldiers(dt: number): void {
		for (const entity of this.entities) {
			if (entity.kind !== 'soldier' || (entity.hp ?? 0) <= 0) {
				continue;
			}

			const target = this.nearestEnemy(entity);
			if (!target) {
				continue;
			}

			const stats = UNIT_STATS[entity.unitType as UnitType] ?? UNIT_STATS[UnitType.Swordsman];
			const dx = target.x - entity.x;
			const dy = target.y - entity.y;
			const dist = Math.hypot(dx, dy) || 0.01;

			if (dist > stats.attackRange) {
				entity.x += (dx / dist) * stats.speed * dt;
				entity.y += (dy / dist) * stats.speed * dt;
			}

			entity.x = clamp(entity.x, 1, this.config.width - 1);
			entity.y = clamp(entity.y, 1, this.config.height - 1);
		}

		// Player melee: damage nearest enemy when close
		if ((this.player.hp ?? 0) > 0) {
			const nearest = this.nearestEnemy(this.player);
			if (nearest) {
				const dx = nearest.x - this.player.x;
				const dy = nearest.y - this.player.y;
				const dist = Math.hypot(dx, dy);
				const attackRange = 5;
				if (dist < attackRange) {
					this.player.attackTimer = (this.player.attackTimer ?? 0) - dt;
					if (this.player.attackTimer <= 0) {
						const damage = this.config.playerDamage ?? 10;
						nearest.hp = (nearest.hp ?? 0) - damage;
						this.player.attackTimer = 0.5;
					}
				}
			}
		}
	}

	private resolveCombat(dt: number): void {
		for (const attacker of this.entities) {
			if (attacker.kind !== 'soldier' || (attacker.hp ?? 0) <= 0) {
				continue;
			}

			const stats = UNIT_STATS[attacker.unitType as UnitType] ?? UNIT_STATS[UnitType.Swordsman];
			attacker.attackTimer = (attacker.attackTimer ?? 0) - dt;
			if (attacker.attackTimer > 0) {
				continue;
			}

			const target = this.nearestEnemy(attacker);
			if (!target) {
				continue;
			}

			const dx = target.x - attacker.x;
			const dy = target.y - attacker.y;
			const dist = Math.hypot(dx, dy);
			if (dist > stats.attackRange) {
				continue;
			}

			let {damage} = stats;
			const defenderType = target.unitType as UnitType | undefined;
			if (defenderType !== undefined) {
				damage *= getDamageMultiplier(attacker.unitType as UnitType, defenderType);
			}

			target.hp = (target.hp ?? 0) - damage;
			attacker.attackTimer = stats.cooldown;
		}
	}

	private reapDead(): void {
		for (let i = this.entities.length - 1; i >= 0; i--) {
			const entity = this.entities[i];
			if (entity.kind === 'soldier' && entity.hp !== undefined && entity.hp <= 0) {
				this.entities.splice(i, 1);
			}
		}
	}

	private nearestEnemy(source: SubworldEntity): SubworldEntity | undefined {
		const team = source.team ?? 0;
		let best: SubworldEntity | undefined;
		let bestDist = Infinity;

		for (const entity of this.entities) {
			if (entity === source) {
				continue;
			}

			if ((entity.kind !== 'soldier' && entity.kind !== 'player') || (entity.hp ?? 0) <= 0) {
				continue;
			}

			if (entity.team === team) {
				continue;
			}

			const dx = entity.x - source.x;
			const dy = entity.y - source.y;
			const dist = dx * dx + dy * dy;
			if (dist < bestDist) {
				bestDist = dist;
				best = entity;
			}
		}

		return best;
	}
}

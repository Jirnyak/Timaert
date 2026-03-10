/**
 * Subworld NPC AI — pure stateless behaviour functions.
 *
 * Three AI modes:
 *  - wander: random walk with grid collision, used by peaceful NPCs
 *  - flee:   wander normally, but sprint away from any hostile within radius
 *  - combat: chase-and-attack driven by target acquisition (see engine)
 *
 * Every NPC has `ai: AiKind` — the engine calls these functions
 * each tick to update positions and velocities.
 */

import type {SubworldEntity, TraversabilityGrid} from './types';
import {tileWalkable} from './engine';

// ── Grid helpers ────────────────────────────────────────────────

function canOccupy(grid: TraversabilityGrid, cx: number, cy: number, r: number): boolean {
	return tileWalkable(grid, cx - r, cy - r)
		&& tileWalkable(grid, cx + r, cy - r)
		&& tileWalkable(grid, cx - r, cy + r)
		&& tileWalkable(grid, cx + r, cy + r)
		&& tileWalkable(grid, cx, cy);
}

function clamp(value: number, lo: number, hi: number): number {
	return Math.max(lo, Math.min(hi, value));
}

// ── Wander AI ───────────────────────────────────────────────────

/**
 * Random-walk behaviour for peaceful NPCs.
 * Picks a random direction every few seconds, bounces off walls.
 */
export function tickWander(
	entity: SubworldEntity,
	dt: number,
	grid: TraversabilityGrid | undefined,
	mapWidth: number,
	mapHeight: number,
	wanderSpeed: number,
	rng: () => number,
): void {
	entity.aiTimer = (entity.aiTimer ?? 0) - dt;
	if (entity.aiTimer <= 0) {
		if (rng() < 0.4) {
			entity.vx = 0;
			entity.vy = 0;
		} else {
			const angle = rng() * Math.PI * 2;
			entity.vx = Math.cos(angle) * wanderSpeed;
			entity.vy = Math.sin(angle) * wanderSpeed;
		}

		entity.aiTimer = 1.5 + rng() * 3;
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

	entity.x = clamp(entity.x, r, mapWidth - r);
	entity.y = clamp(entity.y, r, mapHeight - r);

	if (entity.x <= r) {
		entity.vx = Math.abs(entity.vx);
	}

	if (entity.x >= mapWidth - r) {
		entity.vx = -Math.abs(entity.vx);
	}

	if (entity.y <= r) {
		entity.vy = Math.abs(entity.vy);
	}

	if (entity.y >= mapHeight - r) {
		entity.vy = -Math.abs(entity.vy);
	}
}

// ── Flee AI ─────────────────────────────────────────────────────

const FLEE_RADIUS = 60;
const FLEE_SPEED_MULT = 2.2;

/**
 * Flee behaviour for citizens.
 * When no hostile is nearby, wanders normally.
 * When a hostile is within FLEE_RADIUS, sprints directly away.
 *
 * @param threat — nearest hostile entity (found by the engine via
 *                 the faction relation system), or undefined if none nearby.
 * @param threatDist — distance to that entity.
 */
export function tickFlee(
	entity: SubworldEntity,
	dt: number,
	grid: TraversabilityGrid | undefined,
	mapWidth: number,
	mapHeight: number,
	wanderSpeed: number,
	rng: () => number,
	threat: SubworldEntity | undefined,
	threatDist: number,
): void {
	if (threat && threatDist < FLEE_RADIUS) {
		// Sprint away from the hostile
		const dx = entity.x - threat.x;
		const dy = entity.y - threat.y;
		const length = Math.hypot(dx, dy) || 0.01;
		const fleeSpeed = wanderSpeed * FLEE_SPEED_MULT;
		entity.vx = (dx / length) * fleeSpeed;
		entity.vy = (dy / length) * fleeSpeed;
		entity.aiTimer = 0.5; // Brief cooldown before wander resumes

		const newX = entity.x + entity.vx * dt;
		const newY = entity.y + entity.vy * dt;
		const r = entity.radius;

		if (grid) {
			if (canOccupy(grid, newX, entity.y, r)) {
				entity.x = newX;
			}

			if (canOccupy(grid, entity.x, newY, r)) {
				entity.y = newY;
			}
		} else {
			entity.x = newX;
			entity.y = newY;
		}

		entity.x = clamp(entity.x, r, mapWidth - r);
		entity.y = clamp(entity.y, r, mapHeight - r);
		return;
	}

	// No hostile nearby — normal wander
	tickWander(entity, dt, grid, mapWidth, mapHeight, wanderSpeed, rng);
}

// ── Combat movement AI ──────────────────────────────────────────

/**
 * Chase a target — move toward it if outside attack range, stop if in range.
 */
export function tickCombatMove(
	entity: SubworldEntity,
	target: SubworldEntity,
	speed: number,
	attackRange: number,
	dt: number,
	mapWidth: number,
	mapHeight: number,
): void {
	const dx = target.x - entity.x;
	const dy = target.y - entity.y;
	const dist = Math.hypot(dx, dy) || 0.01;

	if (dist > attackRange) {
		entity.vx = (dx / dist) * speed;
		entity.vy = (dy / dist) * speed;
		entity.x += entity.vx * dt;
		entity.y += entity.vy * dt;
	} else {
		entity.vx = 0;
		entity.vy = 0;
	}

	entity.x = clamp(entity.x, 1, mapWidth - 1);
	entity.y = clamp(entity.y, 1, mapHeight - 1);
}

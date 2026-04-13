// === NPC AI Tick Logic ===
// Separated from npc.ts: pure AI behaviour functions.
// Each function is a reusable AI "brain" — multiple NPC types can share one.

import {
	type NPC, NPCState, type TickContext, type TreeGrid,
} from './npc';
import {wrapCoord, torusDistSq, torusStepToward} from './torus';

// Tick interval must match the value in GameScreen
const TICK_SEC = 0.5;

// ── Spatial grid tree lookup ──

function findNearestTreeGrid(
	grid: TreeGrid, px: number, py: number,
	mw: number, mh: number, halfW: number, halfH: number,
): {x: number; y: number} | undefined {
	const {cells, cellSize, cols, rows} = grid;
	const cx0 = Math.floor(px / cellSize);
	const cy0 = Math.floor(py / cellSize);
	let bestDistSq = 901; // Just above 30*30
	let bestTree: {x: number; y: number} | undefined;

	// Check 3x3 ring of cells (with torus wrap)
	for (let oy = -1; oy <= 1; oy++) {
		for (let ox = -1; ox <= 1; ox++) {
			const gx = ((cx0 + ox) % cols + cols) % cols;
			const gy = ((cy0 + oy) % rows + rows) % rows;
			const bucket = cells.get(gy * cols + gx);
			if (!bucket) {
				continue;
			}

			for (const tree of bucket) {
				let dx = Math.abs(px - tree.x);
				let dy = Math.abs(py - tree.y);
				if (dx > halfW) {
					dx = mw - dx;
				}

				if (dy > halfH) {
					dy = mh - dy;
				}

				if (dx > 30 || dy > 30) {
					continue;
				}

				const dSq = dx * dx + dy * dy;
				if (dSq < bestDistSq) {
					bestDistSq = dSq;
					bestTree = tree;
				}
			}
		}
	}

	return bestTree;
}

function findNearestTreeLinear(
	trees: Array<{x: number; y: number}>,
	px: number, py: number,
	mw: number, mh: number,
): {x: number; y: number} | undefined {
	const halfW = mw / 2;
	const halfH = mh / 2;
	let bestDistSq = 901;
	let bestTree: {x: number; y: number} | undefined;
	for (const tree of trees) {
		let dx = Math.abs(px - tree.x);
		let dy = Math.abs(py - tree.y);
		if (dx > halfW) {
			dx = mw - dx;
		}

		if (dy > halfH) {
			dy = mh - dy;
		}

		if (dx > 30 || dy > 30) {
			continue;
		}

		const dSq = dx * dx + dy * dy;
		if (dSq < bestDistSq) {
			bestDistSq = dSq;
			bestTree = tree;
		}
	}

	return bestTree;
}

function setNpcVisualSpeed(npc: NPC, oldX: number, oldY: number): void {
	const dx = npc.x - oldX;
	const dy = npc.y - oldY;
	const dist = Math.sqrt(dx * dx + dy * dy);
	npc.visualSpeed = dist > 0 ? dist / TICK_SEC : 0;
}

function tryMove(
	npc: NPC, tx: number, ty: number,
	ctx: TickContext,
): boolean {
	const {nx, ny} = torusStepToward(npc.x, npc.y, tx, ty, ctx.mapWidth, ctx.mapHeight);
	const oldX = npc.x;
	const oldY = npc.y;

	npc.x = nx;
	npc.y = ny;
	setNpcVisualSpeed(npc, oldX, oldY);

	// Drain NPC SP (base cost per step)
	npc.sp -= 10;
	if (npc.sp < 0) {
		npc.state = NPCState.Resting;
		npc.stateTimer = 0;
		npc.sp = 0;
	}

	return true;
}

function atTarget(npc: NPC, ctx: TickContext): boolean {
	return torusDistSq(npc.x, npc.y, npc.targetX, npc.targetY, ctx.mapWidth, ctx.mapHeight) < 4;
}

function pickRandomNearby(
	cx: number, cy: number, range: number,
	w: number, h: number,
): {x: number; y: number} {
	return {
		x: wrapCoord(cx + Math.floor(Math.random() * range * 2) - range, w),
		y: wrapCoord(cy + Math.floor(Math.random() * range * 2) - range, h),
	};
}

function homePos(npc: NPC, ctx: TickContext): {x: number; y: number} | undefined {
	if (npc.homeSettlementId < 0) {
		return undefined;
	}

	return ctx.settlementById.get(npc.homeSettlementId);
}

// ── Reusable AI behaviours ─────────────────────────────────────
// Each is a standalone tick function: (npc, ctx) => void.
// NPC type registry maps type → behaviour by reference.

/** Wander near home settlement, return if too far. */
export function aiHomeWanderer(npc: NPC, ctx: TickContext): void {
	const home = homePos(npc, ctx);
	if (!home) {
		return;
	}

	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			const distSq = torusDistSq(npc.x, npc.y, home.x, home.y, ctx.mapWidth, ctx.mapHeight);
			if (distSq > 400) {
				npc.targetX = home.x;
				npc.targetY = home.y;
				npc.state = NPCState.Returning;
			} else {
				const p = pickRandomNearby(home.x, home.y, 12, ctx.mapWidth, ctx.mapHeight);
				npc.targetX = p.x;
				npc.targetY = p.y;
				npc.state = NPCState.Wandering;
			}
		}

		return;
	}

	if (npc.state === NPCState.Wandering || npc.state === NPCState.Returning) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Idle;
			npc.stateTimer = 10 + Math.floor(Math.random() * 20);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
	}
}

/** Find nearest tree → travel → work → return home. */
export function aiWoodcutter(npc: NPC, ctx: TickContext): void {
	const home = homePos(npc, ctx);
	if (!home) {
		return;
	}

	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			const {mapWidth: mw, mapHeight: mh} = ctx;
			const bestTree = ctx.treeGrid
				? findNearestTreeGrid(ctx.treeGrid, npc.x, npc.y, mw, mh, mw / 2, mh / 2)
				: findNearestTreeLinear(ctx.trees, npc.x, npc.y, mw, mh);

			if (bestTree) {
				npc.targetX = bestTree.x;
				npc.targetY = bestTree.y;
				npc.state = NPCState.Traveling;
			} else {
				const p = pickRandomNearby(home.x, home.y, 10, ctx.mapWidth, ctx.mapHeight);
				npc.targetX = p.x;
				npc.targetY = p.y;
				npc.state = NPCState.Wandering;
			}
		}

		return;
	}

	if (npc.state === NPCState.Traveling) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Working;
			npc.stateTimer = 8 + Math.floor(Math.random() * 8);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
		return;
	}

	if (npc.state === NPCState.Working) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			npc.targetX = home.x;
			npc.targetY = home.y;
			npc.state = NPCState.Returning;
		}

		return;
	}

	if (npc.state === NPCState.Wandering || npc.state === NPCState.Returning) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Idle;
			npc.stateTimer = 6 + Math.floor(Math.random() * 12);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
	}
}

/** Travel to another settlement to trade, then return home. */
export function aiTrader(npc: NPC, ctx: TickContext): void {
	const home = homePos(npc, ctx);
	if (!home) {
		return;
	}

	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			const others = ctx.settlements.filter(s => s.id !== npc.homeSettlementId);
			if (others.length > 0) {
				const target = others[Math.floor(Math.random() * others.length)];
				npc.targetSettlementId = target.id;
				npc.targetX = target.x;
				npc.targetY = target.y;
				npc.state = NPCState.Traveling;
			} else {
				npc.stateTimer = 20;
			}
		}

		return;
	}

	if (npc.state === NPCState.Traveling) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Working;
			npc.stateTimer = 15 + Math.floor(Math.random() * 20);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
		return;
	}

	if (npc.state === NPCState.Working) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			npc.targetX = home.x;
			npc.targetY = home.y;
			npc.targetSettlementId = -1;
			npc.state = NPCState.Returning;
		}

		return;
	}

	if (npc.state === NPCState.Returning) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Idle;
			npc.stateTimer = 20 + Math.floor(Math.random() * 30);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
	}
}

/** Nomadic trader: travels between settlements endlessly. */
export function aiNomad(npc: NPC, ctx: TickContext): void {
	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			const others = ctx.settlements.filter(s => s.id !== npc.targetSettlementId);
			if (others.length > 0) {
				const target = others[Math.floor(Math.random() * others.length)];
				npc.targetSettlementId = target.id;
				npc.targetX = target.x;
				npc.targetY = target.y;
				npc.state = NPCState.Traveling;
			} else {
				npc.stateTimer = 10;
			}
		}

		return;
	}

	if (npc.state === NPCState.Traveling) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Idle;
			npc.stateTimer = 10 + Math.floor(Math.random() * 15);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
	}
}

/** Chase player if within range, otherwise wander. */
export function aiAggressive(npc: NPC, ctx: TickContext): void {
	const distToPlayerSq = torusDistSq(npc.x, npc.y, ctx.playerX, ctx.playerY, ctx.mapWidth, ctx.mapHeight);

	if (distToPlayerSq < 100) {
		npc.state = NPCState.Chasing;
		npc.targetX = ctx.playerX;
		npc.targetY = ctx.playerY;
		tryMove(npc, npc.targetX, npc.targetY, ctx);
		return;
	}

	if (npc.state === NPCState.Chasing) {
		npc.state = NPCState.Idle;
		npc.stateTimer = 5 + Math.floor(Math.random() * 10);
		return;
	}

	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			const p = pickRandomNearby(npc.x, npc.y, 20, ctx.mapWidth, ctx.mapHeight);
			npc.targetX = p.x;
			npc.targetY = p.y;
			npc.state = NPCState.Wandering;
		}

		return;
	}

	if (npc.state === NPCState.Wandering) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Idle;
			npc.stateTimer = 8 + Math.floor(Math.random() * 15);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
	}
}

/** Patrol near home settlement; return if strayed too far. */
export function aiPatrol(npc: NPC, ctx: TickContext): void {
	const home = homePos(npc, ctx);
	if (!home) {
		return;
	}

	const distHomeSq = torusDistSq(npc.x, npc.y, home.x, home.y, ctx.mapWidth, ctx.mapHeight);

	if (distHomeSq > 144) {
		npc.targetX = home.x;
		npc.targetY = home.y;
		npc.state = NPCState.Returning;
	}

	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			const p = pickRandomNearby(home.x, home.y, 8, ctx.mapWidth, ctx.mapHeight);
			npc.targetX = p.x;
			npc.targetY = p.y;
			npc.state = NPCState.Patrolling;
		}

		return;
	}

	if (npc.state === NPCState.Patrolling || npc.state === NPCState.Returning) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Idle;
			npc.stateTimer = 6 + Math.floor(Math.random() * 10);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
	}
}

/** Wander + rare teleportation. */
export function aiTeleporter(npc: NPC, ctx: TickContext): void {
	if (npc.teleportCooldown > 0) {
		npc.teleportCooldown--;
	}

	if (npc.teleportCooldown <= 0 && Math.random() < 0.005) {
		const p = pickRandomNearby(npc.x, npc.y, 40, ctx.mapWidth, ctx.mapHeight);
		npc.x = p.x;
		npc.y = p.y;
		npc.teleportCooldown = 50;
		npc.state = NPCState.Idle;
		npc.stateTimer = 10;
		return;
	}

	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			const p = pickRandomNearby(npc.x, npc.y, 15, ctx.mapWidth, ctx.mapHeight);
			npc.targetX = p.x;
			npc.targetY = p.y;
			npc.state = NPCState.Wandering;
		}

		return;
	}

	if (npc.state === NPCState.Wandering) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Idle;
			npc.stateTimer = 12 + Math.floor(Math.random() * 20);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
	}
}

/** Simple random wanderer. */
export function aiWanderer(npc: NPC, ctx: TickContext): void {
	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			const p = pickRandomNearby(npc.x, npc.y, 25, ctx.mapWidth, ctx.mapHeight);
			npc.targetX = p.x;
			npc.targetY = p.y;
			npc.state = NPCState.Wandering;
		}

		return;
	}

	if (npc.state === NPCState.Wandering) {
		if (atTarget(npc, ctx)) {
			npc.state = NPCState.Idle;
			npc.stateTimer = 10 + Math.floor(Math.random() * 15);
			return;
		}

		tryMove(npc, npc.targetX, npc.targetY, ctx);
	}
}

// ── City NPC tick (road-constrained movement) ──

export {setNpcVisualSpeed};
export {torusStepToward} from './torus';

export function tickCityNPCs(
	npcs: NPC[],
	grid: Uint8Array,
	width: number,
	height: number,
): void {
	for (const npc of npcs) {
		if (npc.state === NPCState.Idle) {
			npc.stateTimer--;
			if (npc.stateTimer <= 0) {
				const angle = Math.random() * Math.PI * 2;
				const dist = 5 + Math.random() * 10;
				const tx = Math.floor((npc.x + Math.cos(angle) * dist + width) % width);
				const ty = Math.floor((npc.y + Math.sin(angle) * dist + height) % height);

				const tile = grid[ty * width + tx];
				if (tile === 1 || tile === 0) {
					npc.targetX = tx;
					npc.targetY = ty;
					npc.state = NPCState.Wandering;
				} else {
					npc.stateTimer = 10;
				}
			}
		} else if (npc.state === NPCState.Wandering) {
			const {nx, ny} = torusStepToward(npc.x, npc.y, npc.targetX, npc.targetY, width, height);
			const isDiag = nx !== npc.x && ny !== npc.y;
			const nextTile = grid[ny * width + nx];
			const oldX = npc.x;
			const oldY = npc.y;

			if (nextTile === 1 || nextTile === 0) {
				npc.x = nx;
				npc.y = ny;
			} else if (isDiag) {
				const tileX = grid[npc.y * width + nx];
				const tileY = grid[ny * width + npc.x];
				if (tileX === 1 || tileX === 0) {
					npc.x = nx;
				} else if (tileY === 1 || tileY === 0) {
					npc.y = ny;
				}
			}

			if (npc.x !== oldX || npc.y !== oldY) {
				setNpcVisualSpeed(npc, oldX, oldY);
			}

			if (Math.abs(npc.x - npc.targetX) < 1 && Math.abs(npc.y - npc.targetY) < 1) {
				npc.state = NPCState.Idle;
				npc.stateTimer = 20 + Math.floor(Math.random() * 40);
			}
		}
	}
}

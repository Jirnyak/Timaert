/**
 * Subworld simulation engine.
 *
 * Runs an independent game loop (tick-based) with free-form
 * ARPG movement, grid-based collision via CityGenerator traversability,
 * simple NPC AI, and zone-trigger detection.
 *
 * The main world simulation is frozen while a subworld is active —
 * this engine owns the entire update cycle.
 */

import type {CharacterData} from '../../character/types';
import {
	CityGenerator,
	TILE_HOUSE,
	TILE_ROAD,
	TILE_SQUARE,
} from '../city-generator';
import {createCitizenSpriteSheet, renderPlayerSprite} from './citizen-sprites';
import type {
	SubworldConfig,
	SubworldEntity,
	TraversabilityGrid,
	Vec2,
	ZoneAction,
} from './types';

// ── Constants ───────────────────────────────────────────────────

/** Screen pixels per grid tile (20× zoom over CityGenerator's native 2px/tile). */
const CITY_SCALE = 40;

/** Default grid size for generated maps. */
const MAP_SIZE = 1024;

/** Walk animation frame duration (seconds). */
const WALK_FRAME_DURATION = 0.125;

/** Number of walk animation frames. */
const WALK_FRAME_COUNT = 6;

// ── Helpers ─────────────────────────────────────────────────────

function seededRng(seed: number): () => number {
	let s = seed;
	return () => {
		s = (s * 1_103_515_245 + 12_345) & 0x7F_FF_FF_FF;
		return s / 0x7F_FF_FF_FF;
	};
}

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

// ── Grid collision helpers ──────────────────────────────────────

/** Check if a single tile in the traversability grid is walkable. */
function tileWalkable(grid: TraversabilityGrid, x: number, y: number): boolean {
	const gx = Math.floor(x);
	const gy = Math.floor(y);
	if (gx < 0 || gx >= grid.width || gy < 0 || gy >= grid.height) {
		return false;
	}

	return grid.data[gy * grid.width + gx] > 0;
}

/**
 * Check if a circle at (cx, cy) with radius `r` (in grid coords)
 * can occupy that position without hitting a blocked tile.
 */
function canOccupy(grid: TraversabilityGrid, cx: number, cy: number, r: number): boolean {
	return tileWalkable(grid, cx - r, cy - r)
		&& tileWalkable(grid, cx + r, cy - r)
		&& tileWalkable(grid, cx - r, cy + r)
		&& tileWalkable(grid, cx + r, cy + r)
		&& tileWalkable(grid, cx, cy);
}

/**
 * Find a walkable position near (cx, cy) within `searchRadius`.
 * Returns the first walkable tile found via spiral search.
 */
function findWalkable(
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

	/** Advance simulation by `dt` seconds. */
	tick(dt: number): void {
		this.updatePlayer(dt);
		this.updateNpcs(dt);
		this.updateAnimations(dt);
		this.resolveCollisions();
		this.checkZones();
	}

	/** Consume (and clear) the pending zone action, if any. */
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

	// ── Internals ─────────────────────────────────────────────

	private updatePlayer(dt: number): void {
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
			// Axis-independent grid collision for smooth wall sliding
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

		// Clamp to world bounds
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
					// Pick a random direction or stop
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

				// Bounce off world edges
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

			// 'idle' and 'patrol' are no-ops for now (extendable)
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

	private resolveCollisions(): void {
		// Push player out of solid circle entities (NPC–player)
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

		// Re-clamp after push
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
}

// ── Subworld factory helpers ────────────────────────────────────

function makeEntity(nextId: {value: number}, partial: Partial<SubworldEntity> & {kind: SubworldEntity['kind']}): SubworldEntity {
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

/**
 * Scan the CityGenerator grid to find a tile of the given type
 * near the target coordinates.
 */
function findTileNear(
	grid: Uint8Array,
	gridWidth: number,
	gridHeight: number,
	targetX: number,
	targetY: number,
	tileType: number,
	searchRadius: number,
): Vec2 | undefined {
	for (let r = 0; r < searchRadius; r++) {
		const match = scanRing(grid, gridWidth, gridHeight, targetX, targetY, tileType, r);
		if (match) {
			return match;
		}
	}

	return undefined;
}

function scanRing(
	grid: Uint8Array,
	gridWidth: number,
	gridHeight: number,
	targetX: number,
	targetY: number,
	tileType: number,
	r: number,
): Vec2 | undefined {
	for (let dy = -r; dy <= r; dy++) {
		for (let dx = -r; dx <= r; dx++) {
			if (Math.abs(dx) !== r && Math.abs(dy) !== r) {
				continue;
			}

			const gx = Math.floor(targetX) + dx;
			const gy = Math.floor(targetY) + dy;
			if (gx >= 0 && gx < gridWidth && gy >= 0 && gy < gridHeight
				&& grid[gy * gridWidth + gx] === tileType) {
				return {x: gx + 0.5, y: gy + 0.5};
			}
		}
	}

	return undefined;
}

/**
 * Find a random TILE_ROAD tile that is adjacent (within 3 tiles)
 * to at least one TILE_HOUSE tile.  This places NPCs on streets
 * near buildings rather than in random open space.
 */
function findRoadNearHouses(
	grid: Uint8Array,
	gridWidth: number,
	gridHeight: number,
	rng: () => number,
): Vec2 | undefined {
	for (let attempt = 0; attempt < 200; attempt++) {
		const gx = Math.floor(rng() * gridWidth);
		const gy = Math.floor(rng() * gridHeight);
		if (grid[gy * gridWidth + gx] !== TILE_ROAD) {
			continue;
		}

		// Check if any tile within radius 3 is a house
		let nearHouse = false;
		for (let dy = -3; dy <= 3 && !nearHouse; dy++) {
			for (let dx = -3; dx <= 3 && !nearHouse; dx++) {
				const nx = gx + dx;
				const ny = gy + dy;
				if (nx >= 0 && nx < gridWidth && ny >= 0 && ny < gridHeight
					&& grid[ny * gridWidth + nx] === TILE_HOUSE) {
					nearHouse = true;
				}
			}
		}

		if (nearHouse) {
			return {x: gx + 0.5, y: gy + 0.5};
		}
	}

	return undefined;
}

// ── Settlement subworld factory ─────────────────────────────────

export type SettlementSubworldOptions = {
	seed: number;
	name: string;
	population: number;
	economy: string;
	mood: string;
	characterData: CharacterData;
};

/**
 * Generate a SubworldConfig for a settlement using CityGenerator.
 * Uses the generated map as background and the traversability
 * grid for collision.  Entities are placed on walkable tiles.
 * NPC count equals settlement population.
 */
export async function createSettlementSubworld(options: SettlementSubworldOptions): Promise<SubworldConfig> {
	const generator = new CityGenerator(options.seed, MAP_SIZE, MAP_SIZE, 'city');
	const mapData = generator.generate(options.population);
	const rng = seededRng(options.seed + 7);

	const traversabilityRaw = mapData.grid;
	const traversability: TraversabilityGrid = {
		width: mapData.width,
		height: mapData.height,
		data: traversabilityRaw,
	};

	const nextId = {value: 0};
	const entities: SubworldEntity[] = [];

	// Player at map center (spawn point from generator)
	entities.push(makeEntity(nextId, {
		kind: 'player',
		x: mapData.spawnX,
		y: mapData.spawnY,
		radius: 0.5,
		solid: true,
		label: 'You',
		color: '#4af',
	}));

	// Exit zones at the four edges (where main roads meet the border)
	const exitPositions: Array<{x: number; y: number}> = [
		{x: mapData.spawnX, y: 6},
		{x: mapData.spawnX, y: mapData.height - 6},
		{x: 6, y: mapData.spawnY},
		{x: mapData.width - 6, y: mapData.spawnY},
	];

	for (const position of exitPositions) {
		entities.push(makeEntity(nextId, {
			kind: 'zone',
			x: position.x,
			y: position.y,
			radius: 48,
			label: 'Exit',
			color: 'rgba(255,80,80,0.25)',
			action: {type: 'exit'},
		}));
	}

	// Trade zone near the center square
	const tileGrid = generator.getTileGrid();
	const tradeSpot = findTileNear(tileGrid, mapData.width, mapData.height, mapData.spawnX, mapData.spawnY, TILE_SQUARE, 30);

	if (tradeSpot) {
		entities.push(makeEntity(nextId, {
			kind: 'zone',
			x: tradeSpot.x + 3,
			y: tradeSpot.y + 3,
			radius: 8,
			label: 'Market',
			color: 'rgba(255,255,100,0.2)',
			action: {type: 'trade'},
		}));
	}

	// Rest zone (inn) — find a road tile near center
	const innSpot = findTileNear(tileGrid, mapData.width, mapData.height, mapData.spawnX + 20, mapData.spawnY - 15, TILE_ROAD, 40);

	if (innSpot) {
		const cost = options.mood === 'Prosperous' ? 5 : (options.mood === 'Tense' ? 15 : 10);
		entities.push(makeEntity(nextId, {
			kind: 'zone',
			x: innSpot.x,
			y: innSpot.y,
			radius: 8,
			label: 'Inn',
			color: 'rgba(100,200,255,0.2)',
			action: {type: 'rest', cost},
		}));
	}

	// Spawn wandering NPCs — one per population unit
	const citizenSheet = await createCitizenSpriteSheet(options.population);
	const playerSheet = await renderPlayerSprite(options.characterData);
	const numberNpcs = options.population;
	const npcNames = ['Villager', 'Guard', 'Merchant', 'Peasant', 'Scholar', 'Beggar'];
	for (let i = 0; i < numberNpcs; i++) {
		const spot = findRoadNearHouses(tileGrid, mapData.width, mapData.height, rng);

		if (spot) {
			entities.push(makeEntity(nextId, {
				kind: 'npc',
				x: spot.x,
				y: spot.y,
				radius: 0.5,
				solid: true,
				label: npcNames[Math.floor(rng() * npcNames.length)],
				color: `hsl(${Math.floor(rng() * 360)}, 40%, 55%)`,
				ai: 'wander',
				aiTimer: rng() * 3,
				spriteIndex: i % citizenSheet.count,
			}));
		}
	}

	return {
		seed: options.seed,
		width: mapData.width,
		height: mapData.height,
		bgColor: '#3a4a2a',
		groundColorA: '#4a5a3a',
		groundColorB: '#3e5235',
		entities,
		name: options.name,
		bgImage: mapData.visual,
		traversability,
		scale: CITY_SCALE,
		citizenSheet,
		playerSheet,
	};
}

// ── Nature subworld factory ─────────────────────────────────────

export type NatureSubworldOptions = {
	seed: number;
	name: string;
	characterData: CharacterData;
};

/**
 * Generate a SubworldConfig for wilderness exploration
 * using CityGenerator in 'nature' mode.
 */
export async function createNatureSubworld(options: NatureSubworldOptions): Promise<SubworldConfig> {
	const generator = new CityGenerator(options.seed, MAP_SIZE, MAP_SIZE, 'nature');
	const mapData = generator.generate(500);
	const rng = seededRng(options.seed + 13);
	const playerSheet = await renderPlayerSprite(options.characterData);

	const traversabilityRaw = mapData.grid;
	const traversability: TraversabilityGrid = {
		width: mapData.width,
		height: mapData.height,
		data: traversabilityRaw,
	};

	const nextId = {value: 0};
	const entities: SubworldEntity[] = [];

	// Player at center
	entities.push(makeEntity(nextId, {
		kind: 'player',
		x: mapData.spawnX,
		y: mapData.spawnY,
		radius: 0.5,
		solid: true,
		label: 'You',
		color: '#4af',
	}));

	// Exit zones at the four edges
	const exitPositions: Array<{x: number; y: number}> = [
		{x: mapData.spawnX, y: 6},
		{x: mapData.spawnX, y: mapData.height - 6},
		{x: 6, y: mapData.spawnY},
		{x: mapData.width - 6, y: mapData.spawnY},
	];

	for (const position of exitPositions) {
		entities.push(makeEntity(nextId, {
			kind: 'zone',
			x: position.x,
			y: position.y,
			radius: 48,
			label: 'Exit',
			color: 'rgba(255,80,80,0.25)',
			action: {type: 'exit'},
		}));
	}

	// Scatter a few NPCs / creatures in the wilds
	const creatureNames = ['Deer', 'Wolf', 'Rabbit', 'Fox', 'Bear'];
	const numberCreatures = 4 + Math.floor(rng() * 4);
	for (let i = 0; i < numberCreatures; i++) {
		const searchRange = Math.min(300, mapData.width / 2);
		const spot = findWalkable(traversability, rng, mapData.spawnX, mapData.spawnY, searchRange);

		if (spot) {
			entities.push(makeEntity(nextId, {
				kind: 'npc',
				x: spot.x,
				y: spot.y,
				radius: 0.5,
				solid: true,
				label: creatureNames[Math.floor(rng() * creatureNames.length)],
				color: `hsl(${Math.floor(rng() * 120)}, 35%, 45%)`,
				ai: 'wander',
				aiTimer: rng() * 3,
			}));
		}
	}

	// A clearing spot for dialog / rest
	const natureTileGrid = generator.getTileGrid();
	const clearingSpot = findTileNear(natureTileGrid, mapData.width, mapData.height, mapData.spawnX, mapData.spawnY, TILE_SQUARE, 60);

	if (clearingSpot) {
		entities.push(makeEntity(nextId, {
			kind: 'zone',
			x: clearingSpot.x,
			y: clearingSpot.y,
			radius: 40,
			label: 'Clearing',
			color: 'rgba(100,255,100,0.15)',
			action: {type: 'dialog', text: 'A peaceful clearing in the woods. You can rest here.'},
		}));
	}

	return {
		seed: options.seed,
		width: mapData.width,
		height: mapData.height,
		bgColor: '#1a2a0a',
		groundColorA: '#2a3a1a',
		groundColorB: '#253215',
		entities,
		name: options.name,
		bgImage: mapData.visual,
		traversability,
		scale: CITY_SCALE,
		playerSheet,
	};
}

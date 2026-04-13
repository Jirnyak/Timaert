/**
 * Subworld entity spawning — converts macroworld data into microworld NPCs.
 *
 * Single derivation path for all micro-entities:
 *   CombatTemplate + level → SubworldEntity combat stats.
 *
 * Both army soldiers and city NPCs are microworld NPCs —
 * soldiers are just NPCs whose CombatTemplate comes from UNIT_STATS
 * rather than NPC_TYPE_DEFS.
 *
 * Universal populator: populateCell() handles all NPC spawning for a
 * single cell — city citizens, macroworld squads, biome fauna — in one
 * call. Adding a new macroworld NPC type or army automatically appears
 * in the subworld.
 */

import {
	type ArmyComposition, type CombatTemplate, ALL_UNIT_TYPES, UNIT_STATS,
} from '../army';
import {NPC_TYPE_DEFS, NPCType, type NPC} from '../npc';
import type {Biome} from '../biomes';
import type {SubworldEntity, TraversabilityGrid, AiKind} from './types';
import type {CellFeature} from './map-data';
import {makeEntity, findWalkable} from './engine';
import {getFaunaTable, rollFauna} from './fauna';

// ── Combat stat derivation ──────────────────────────────────────

/**
 * Scale a CombatTemplate by level. Level 1 = base stats.
 * Each additional level adds 15% HP and damage.
 */
function scaledStats(template: CombatTemplate, level: number): {
	hp: number; damage: number; speed: number;
	attackRange: number; cooldown: number;
} {
	const scale = 1 + (level - 1) * 0.15;
	return {
		hp: Math.floor(template.hp * scale),
		damage: Math.floor(template.damage * scale),
		speed: template.speed,
		attackRange: template.attackRange,
		cooldown: template.cooldown,
	};
}

// ── Generic micro-NPC factory ───────────────────────────────────

type MicroNpcOptions = {
	template: CombatTemplate;
	level: number;
	x: number;
	y: number;
	label: string;
	color: string;
	factionId: string;
	ai: AiKind;
	radius?: number;
	/** UnitType enum for army soldiers (enables rock-paper-scissors damage). */
	unitType?: number;
	/** NPCType enum for spawned NPCs. */
	npcType?: number;
	spriteIndex?: number;
};

/**
 * Create a microworld NPC entity from macro-level data.
 * Works for both army soldiers and city/wilderness NPCs.
 */
export function createMicroNpc(
	nextId: {value: number},
	options: MicroNpcOptions,
): SubworldEntity {
	const stats = scaledStats(options.template, options.level);
	return makeEntity(nextId, {
		kind: 'npc',
		x: options.x,
		y: options.y,
		radius: options.radius ?? 1,
		solid: true,
		label: options.label,
		color: options.color,
		hp: stats.hp,
		maxHp: stats.hp,
		damage: stats.damage,
		speed: stats.speed,
		attackRange: stats.attackRange,
		cooldown: stats.cooldown,
		factionId: options.factionId,
		ai: options.ai,
		aiTimer: 0,
		unitType: options.unitType,
		npcType: options.npcType,
		spriteIndex: options.spriteIndex,
	});
}

// ── Army spawning ───────────────────────────────────────────────

/**
 * Spawn an army composition as microworld NPC entities.
 * Each soldier's stats are derived from UNIT_STATS via the
 * universal CombatTemplate → micro-entity path.
 */
export function spawnArmy(
	army: ArmyComposition,
	factionId: string,
	factionLabel: string,
	colors: Record<number, string>,
	cx: number,
	cy: number,
	spread: number,
	nextId: {value: number},
	traversability: TraversabilityGrid,
	rng: () => number,
): SubworldEntity[] {
	const entities: SubworldEntity[] = [];
	for (const ut of ALL_UNIT_TYPES) {
		const qty = army[ut] ?? 0;
		const template = UNIT_STATS[ut];
		for (let i = 0; i < qty; i++) {
			const spot = findWalkable(traversability, rng, cx, cy, spread);
			if (spot) {
				entities.push(createMicroNpc(nextId, {
					template,
					level: 1,
					x: spot.x,
					y: spot.y,
					label: factionLabel
						? `${factionLabel} ${template.label}`
						: template.label,
					color: colors[ut as number] ?? '#888',
					factionId,
					ai: 'combat',
					unitType: ut as number,
				}));
			}
		}
	}

	return entities;
}

// ── City NPC spawning ───────────────────────────────────────────

type CityNpcDistribution = Array<{type: NPCType; weight: number}>;

/**
 * Spawn city population as microworld NPC entities.
 * Combat stats are derived from NPC_TYPE_DEFS.combat via the
 * universal CombatTemplate → micro-entity path.
 */
export function spawnCityNpcs(
	count: number,
	cityFaction: string,
	distribution: CityNpcDistribution,
	guardTypes: ReadonlySet<NPCType>,
	nextId: {value: number},
	rng: () => number,
	findSpot: () => {x: number; y: number} | undefined,
	citizenSheetCount?: number,
): SubworldEntity[] {
	const entities: SubworldEntity[] = [];
	for (let i = 0; i < count; i++) {
		const spot = findSpot();
		if (!spot) {
			continue;
		}

		// Pick NPC type from weighted distribution
		let roll = rng();
		let nType = NPCType.Peasant;
		for (const entry of distribution) {
			roll -= entry.weight;
			if (roll <= 0) {
				nType = entry.type;
				break;
			}
		}

		const def = NPC_TYPE_DEFS[nType] ?? NPC_TYPE_DEFS[NPCType.Peasant];
		const npcLevel = def.baseLevel + Math.floor(rng() * 3);
		const isGuard = guardTypes.has(nType);

		entities.push(createMicroNpc(nextId, {
			template: def.combat,
			level: npcLevel,
			x: spot.x,
			y: spot.y,
			label: def.names[Math.floor(rng() * def.names.length)],
			color: `hsl(${Math.floor(rng() * 360)}, 40%, 55%)`,
			factionId: cityFaction,
			ai: isGuard ? 'combat' : 'flee',
			radius: 0.5,
			npcType: nType,
			spriteIndex: citizenSheetCount ? i % citizenSheetCount : undefined,
		}));
	}

	return entities;
}

// ── Wilderness NPC spawning ─────────────────────────────────────

/**
 * Spawn hostile wilderness NPCs (bandits, etc.) as microworld entities.
 */
export function spawnWildernessNpcs(
	npcType: NPCType,
	count: number,
	factionId: string,
	color: string,
	nextId: {value: number},
	traversability: TraversabilityGrid,
	rng: () => number,
	cx: number,
	cy: number,
	spread: number,
	ai: AiKind = 'wander',
): SubworldEntity[] {
	const entities: SubworldEntity[] = [];
	const def = NPC_TYPE_DEFS[npcType] ?? NPC_TYPE_DEFS[NPCType.Peasant];
	for (let i = 0; i < count; i++) {
		const spot = findWalkable(traversability, rng, cx, cy, spread);
		if (spot) {
			const level = def.baseLevel + Math.floor(rng() * 3);
			entities.push(createMicroNpc(nextId, {
				template: def.combat,
				level,
				x: spot.x,
				y: spot.y,
				label: def.names[Math.floor(rng() * def.names.length)],
				color,
				factionId,
				ai,
				radius: 1.2,
				npcType: npcType as number,
			}));
		}
	}

	return entities;
}

// ── Macroworld NPC → subworld entity spawning ───────────────────

/** Map macroworld NPC AI to subworld AI kind. */
function macroAiToSubworldAi(npc: NPC): AiKind {
	switch (npc.type) {
		case NPCType.Bandit: {return 'combat';
		}

		case NPCType.Guard: {return 'combat';
		}

		case NPCType.Witch: {return 'combat';
		}

		case NPCType.Sorceress: {return 'combat';
		}

		case NPCType.Peasant: {return 'flee';
		}

		case NPCType.Merchant: {return 'flee';
		}

		case NPCType.Woodcutter: {return 'wander';
		}

		case NPCType.Caravan: {return 'wander';
		}
	}
}

/**
 * Spawn macroworld NPCs that occupy a given cell into the subworld.
 * Works for any NPC type — bandits, caravans, merchants, witches, etc.
 * Each NPC's army (if any) is also spawned as individual soldiers.
 */
export function spawnMacroNpcs(
	npcs: NPC[],
	nextId: {value: number},
	traversability: TraversabilityGrid,
	rng: () => number,
	cx: number,
	cy: number,
	spread: number,
): SubworldEntity[] {
	const entities: SubworldEntity[] = [];
	for (const npc of npcs) {
		const def = NPC_TYPE_DEFS[npc.type] ?? NPC_TYPE_DEFS[NPCType.Peasant];
		const spot = findWalkable(traversability, rng, cx, cy, spread);
		if (!spot) {
			continue;
		}

		// Spawn the NPC leader
		entities.push(createMicroNpc(nextId, {
			template: def.combat,
			level: npc.level,
			x: spot.x,
			y: spot.y,
			label: npc.name,
			color: npc.factionId === 'cults' ? '#cc4444' : `hsl(${Math.abs((npc.name.codePointAt(0) ?? 0) * 37) % 360}, 40%, 55%)`,
			factionId: npc.factionId || 'empire',
			ai: macroAiToSubworldAi(npc),
			radius: 1,
			npcType: npc.type as number,
		}));

		// Spawn army units around the leader (if NPC has an army)
		const {army} = npc;
		if (army) {
			const unitColors: Record<number, string> = {
				0: '#888', 1: '#888', 2: '#888', 3: '#888',
			};
			const armyEntities = spawnArmy(army, npc.factionId || 'empire', npc.name, unitColors, spot.x, spot.y, 30, nextId, traversability, rng);
			entities.push(...armyEntities);
		}
	}

	return entities;
}

// ── Fauna spawning ──────────────────────────────────────────────

/**
 * Spawn biome-appropriate fauna entities for a cell.
 * Reads fauna tables from fauna.ts — fully data-driven.
 */
export function spawnFauna(
	biome: Biome,
	feature: CellFeature,
	landmark: string | undefined,
	nextId: {value: number},
	traversability: TraversabilityGrid,
	rng: () => number,
	cx: number,
	cy: number,
	spread: number,
): SubworldEntity[] {
	const table = getFaunaTable(biome, feature, landmark);
	const picks = rollFauna(table, rng);
	const entities: SubworldEntity[] = [];

	for (const pick of picks) {
		const spot = findWalkable(traversability, rng, cx, cy, spread);
		if (!spot) {
			continue;
		}

		const level = pick.baseLevel + Math.floor(rng() * 2);
		entities.push(createMicroNpc(nextId, {
			template: pick.combat,
			level,
			x: spot.x,
			y: spot.y,
			label: pick.label,
			color: pick.color,
			factionId: pick.factionId,
			ai: pick.ai,
			radius: pick.radius,
		}));
	}

	return entities;
}

// ── Universal cell populator ────────────────────────────────────

/** Context for populating a single subworld cell. */
export type PopulateCellContext = {
	/** Macroworld cell coords. */
	cellX: number;
	cellY: number;
	/** Cell biome, feature, landmark from macroworld. */
	biome: Biome;
	feature: CellFeature;
	landmark: string | undefined;
	landmarkParam: number;
	/** Center position in global (composite) subworld coords. */
	globalCx: number;
	globalCy: number;
	/** Cell seed. */
	seed: number;
	/** Composite traversability grid. */
	traversability: TraversabilityGrid;
	/** Macroworld NPCs inside this cell. */
	macroNpcs: NPC[];
	/** City faction id (for city/village cells). */
	cityFaction?: string;
	/** Find spawn position for city NPCs (on roads near houses). */
	findCitySpot?: () => {x: number; y: number} | undefined;
	/** Number of citizens on the sprite sheet (for sprite assignment). */
	citizenSheetCount?: number;
};

/**
 * Universal cell populator — spawns all entities for a single subworld cell.
 *
 * Handles:
 * 1. City/village citizens (from population) with trade zone + inn
 * 2. Macroworld NPC squads (caravans, bandits, witches, etc.)
 * 3. Biome-appropriate fauna (animals + monsters)
 *
 * Any new macroworld NPC type or army is automatically loaded.
 * Any new fauna entry in fauna.ts is automatically spawned.
 */
export function populateCell(
	ctx: PopulateCellContext,
	nextId: {value: number},
	rng: () => number,
): SubworldEntity[] {
	const entities: SubworldEntity[] = [];
	const spread = 400; // ~40% of CELL_SIZE

	// 1. City/village citizens from population
	if ((ctx.landmark === 'city' || ctx.landmark === 'village') && ctx.findCitySpot) {
		const npcDistribution: Array<{type: NPCType; weight: number}> = [
			{type: NPCType.Peasant, weight: 0.55},
			{type: NPCType.Merchant, weight: 0.2},
			{type: NPCType.Woodcutter, weight: 0.2},
			{type: NPCType.Witch, weight: 0.05},
			{type: NPCType.Guard, weight: 0},
			{type: NPCType.Sorceress, weight: 0},
		];
		const guardTypes = new Set([NPCType.Guard, NPCType.Sorceress]);
		const faction = ctx.cityFaction ?? 'empire';
		entities.push(...spawnCityNpcs(ctx.landmarkParam, faction, npcDistribution, guardTypes, nextId, rng, ctx.findCitySpot, ctx.citizenSheetCount));
	}

	// 2. Macroworld NPC squads in this cell
	if (ctx.macroNpcs.length > 0) {
		entities.push(...spawnMacroNpcs(ctx.macroNpcs, nextId, ctx.traversability, rng, ctx.globalCx, ctx.globalCy, spread));
	}

	// 3. Biome fauna (animals + monsters)
	entities.push(...spawnFauna(ctx.biome, ctx.feature, ctx.landmark, nextId, ctx.traversability, rng, ctx.globalCx, ctx.globalCy, spread));

	return entities;
}

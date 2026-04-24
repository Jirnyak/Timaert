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
 * Derive subworld combat stats from a CombatTemplate using:
 *   • base stats (persistent, part of the NPC's design)
 *   • RPG level (the NPC's persistent character level)
 *   • macroworld context multipliers (zone power, landmark, conditions…)
 *
 * Each level adds 15% HP and damage. Context multipliers stack
 * multiplicatively on top — fully data-driven, no hardcoded scaling.
 */
function deriveStats(template: CombatTemplate, level: number, ctx?: ContextScale): {
	hp: number; damage: number; speed: number;
	attackRange: number; cooldown: number;
} {
	const levelScale = 1 + (level - 1) * 0.15;
	const hpMult = (ctx?.hpMult ?? 1) * levelScale;
	const dmgMult = (ctx?.damageMult ?? 1) * levelScale;
	return {
		hp: Math.floor(template.hp * hpMult),
		damage: Math.floor(template.damage * dmgMult),
		speed: template.speed * (ctx?.speedMult ?? 1),
		attackRange: template.attackRange,
		cooldown: template.cooldown,
	};
}

/** Macroworld-derived multipliers applied to base CombatTemplate stats. */
export type ContextScale = {
	hpMult?: number;
	damageMult?: number;
	speedMult?: number;
	/** Bonus levels added on top of the NPC's persistent level. */
	levelBonus?: number;
};

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
	/** Macroworld NPC id (leader or any soldier of the macro squad). */
	macroNpcId?: number;
	/** UnitType enum for army soldiers (enables rock-paper-scissors damage). */
	unitType?: number;
	/** NPCType enum for spawned NPCs. */
	npcType?: number;
	spriteIndex?: number;
	/** Optional macroworld context multipliers (zone power, landmark size, etc.). */
	contextScale?: ContextScale;
};

/**
 * Create a microworld NPC entity from macro-level data.
 * Works for both army soldiers and city/wilderness NPCs.
 */
export function createMicroNpc(
	nextId: {value: number},
	options: MicroNpcOptions,
): SubworldEntity {
	const ctx = options.contextScale;
	const effectiveLevel = options.level + (ctx?.levelBonus ?? 0);
	const stats = deriveStats(options.template, effectiveLevel, ctx);
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
		attackKind: options.template.attackKind ?? 'melee',
		missileSpeed: options.template.missileSpeed,
		missileBlast: options.template.missileBlast,
		missileColor: options.template.missileColor,
		factionId: options.factionId,
		ai: options.ai,
		aiTimer: 0,
		unitType: options.unitType,
		npcType: options.npcType,
		macroNpcId: options.macroNpcId,
		spriteIndex: options.spriteIndex,
		level: effectiveLevel,
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
	contextScale?: ContextScale,
	macroNpcId?: number,
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
					contextScale,
					macroNpcId,
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
	contextScale?: ContextScale,
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
			contextScale,
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

/** Per-faction tint for macro NPC sprites in the subworld. */
const FACTION_COLORS: Record<string, string> = {
	bandits: '#7a3a1a',
	demons: '#8b0000',
	cults: '#cc4444',
	wildlife: '#6b8e23',
};

function colorForFaction(factionId: string | undefined, name: string): string {
	if (factionId && FACTION_COLORS[factionId]) {
		return FACTION_COLORS[factionId];
	}

	return `hsl(${Math.abs((name.codePointAt(0) ?? 0) * 37) % 360}, 40%, 55%)`;
}

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
	contextScale?: ContextScale,
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
			color: colorForFaction(npc.factionId, npc.name),
			factionId: npc.factionId || 'empire',
			ai: macroAiToSubworldAi(npc),
			radius: 1,
			npcType: npc.type as number,
			macroNpcId: npc.id,
			contextScale,
		}));

		// Spawn army units around the leader (if NPC has an army)
		const {army} = npc;
		if (army) {
			const unitColors: Record<number, string> = {
				0: '#888', 1: '#888', 2: '#888', 3: '#888',
			};
			const armyEntities = spawnArmy(army, npc.factionId || 'empire', npc.name, unitColors, spot.x, spot.y, 30, nextId, traversability, rng, contextScale, npc.id);
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
	contextScale?: ContextScale,
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
			factionId: table.factionOverride ?? pick.factionId,
			ai: pick.ai,
			radius: pick.radius,
			contextScale,
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
	/** Difficulty zone (0-9) — scales monster level + combat. */
	zoneLevel?: number;
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
	const scale = deriveContextScale(ctx);

	// 1. City/village citizens from population
	if ((ctx.landmark === 'city' || ctx.landmark === 'village') && ctx.findCitySpot) {
		const civilianDistribution: Array<{type: NPCType; weight: number}> = [
			{type: NPCType.Peasant, weight: 0.55},
			{type: NPCType.Merchant, weight: 0.21},
			{type: NPCType.Woodcutter, weight: 0.21},
			{type: NPCType.Witch, weight: 0.03},
		];
		const guardDistribution: Array<{type: NPCType; weight: number}> = [
			{type: NPCType.Guard, weight: 1},
		];
		const guardTypes = new Set([NPCType.Guard, NPCType.Sorceress]);
		const faction = ctx.cityFaction ?? 'empire';
		const total = Math.max(0, ctx.landmarkParam);
		// Guards are 10% of total population (not added on top).
		const guardCount = Math.floor(total * 0.1);
		const civilianCount = total - guardCount;
		entities.push(
			...spawnCityNpcs(civilianCount, faction, civilianDistribution, guardTypes, nextId, rng, ctx.findCitySpot, ctx.citizenSheetCount, scale),
			...spawnCityNpcs(guardCount, faction, guardDistribution, guardTypes, nextId, rng, ctx.findCitySpot, ctx.citizenSheetCount, scale),
		);
	}

	// 2. Macroworld NPC squads in this cell
	if (ctx.macroNpcs.length > 0) {
		entities.push(...spawnMacroNpcs(ctx.macroNpcs, nextId, ctx.traversability, rng, ctx.globalCx, ctx.globalCy, spread, scale));
	}

	// 3. Biome fauna (animals + monsters)
	entities.push(...spawnFauna(ctx.biome, ctx.feature, ctx.landmark, nextId, ctx.traversability, rng, ctx.globalCx, ctx.globalCy, spread, scale));

	return entities;
}

/**
 * Derive macroworld context multipliers for an NPC's combat stats.
 *
 * Pure data-driven. Currently models:
 *   • Bigger settlements field stronger garrisons (+1 level per √(pop/100)).
 *   • Wilderness in deep wilderness biomes (no landmark) yields no bonus.
 *
 * Add new modifiers (event-driven, biome-driven, plot-driven) as new
 * lines here — every spawned NPC inherits them automatically.
 */
function deriveContextScale(ctx: PopulateCellContext): ContextScale {
	const scale: ContextScale = {};
	let levelBonus = 0;

	if (ctx.landmark === 'city' || ctx.landmark === 'village') {
		const pop = Math.max(0, ctx.landmarkParam);
		levelBonus += Math.floor(Math.sqrt(pop / 100));
	}

	// Zone-driven scaling — wilderness gets tougher with depth.
	// Zones 0-2 are safe; each zone above 2 adds +1 level + small stat boost.
	const zone = Math.max(0, ctx.zoneLevel ?? 0);
	if (zone > 2) {
		const zoneBonus = zone - 2; // 1..7
		levelBonus += zoneBonus;
		const statBoost = 1 + (zoneBonus * 0.18);
		scale.hpMult = statBoost;
		scale.damageMult = statBoost;
	}

	if (levelBonus > 0) {
		scale.levelBonus = levelBonus;
	}

	return scale;
}

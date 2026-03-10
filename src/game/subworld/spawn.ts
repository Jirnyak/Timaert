/**
 * Subworld entity spawning — converts macroworld data into microworld NPCs.
 *
 * Single derivation path for all micro-entities:
 *   CombatTemplate + level → SubworldEntity combat stats.
 *
 * Both army soldiers and city NPCs are microworld NPCs —
 * soldiers are just NPCs whose CombatTemplate comes from UNIT_STATS
 * rather than NPC_TYPE_DEFS.
 */

import {
	type ArmyComposition, type CombatTemplate, ALL_UNIT_TYPES, UNIT_STATS,
} from '../army';
import {NPC_TYPE_DEFS, NPCType} from '../npc';
import type {SubworldEntity, TraversabilityGrid, AiKind} from './types';
import {makeEntity, findWalkable} from './engine';

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

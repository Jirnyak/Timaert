// === NPC System ===
// Type registry + spawning. AI lives in npc-ai.ts.

import type {CharacterData} from '../character/types';
import {CharacterManager} from '../character/character-generator';
import {paletteManager} from '../character/palette';
import {type Inventory, createInventory, generateNpcInventory} from './items';
import {
	type ArmyComposition, type CombatTemplate, UnitType, defaultArmy,
} from './army';
import {
	aiHomeWanderer, aiWoodcutter, aiTrader, aiNomad,
	aiAggressive, aiPatrol, aiTeleporter, aiWanderer,
} from './npc-ai';
import {xorshift32} from './rng';
import {wrapCoord} from './torus';

export {SPRITE_CITY} from './renderer';
export {tickCityNPCs} from './npc-ai';

// ── NPC Types (matches old_concept/src/core/types.h) ──

export const enum NPCType {
	Peasant = 0,
	Woodcutter = 1,
	Merchant = 2,
	Caravan = 3,
	Bandit = 4,
	Guard = 5,
	Witch = 6,
	Sorceress = 7,
}

export const enum NPCState {
	Idle = 0,
	Wandering = 1,
	Traveling = 2,
	Returning = 3,
	Working = 4,
	Chasing = 5,
	Patrolling = 6,
}

export type NPCTrait = 'Greedy' | 'Honorable' | 'Cowardly' | 'Brave' | 'Aggressive' | 'Generous' | 'Suspicious' | 'Curious';
const ALL_TRAITS: NPCTrait[] = ['Greedy', 'Honorable', 'Cowardly', 'Brave', 'Aggressive', 'Generous', 'Suspicious', 'Curious'];

export type NPC = {
	id: number;
	name: string;
	type: NPCType;
	factionId: string; // Empire, magika, barbarians, timaert, cults or ""
	x: number;
	y: number;
	visualX: number;
	visualY: number;
	visualSpeed: number; // Tiles/sec — set when logical position changes
	homeSettlementId: number;
	targetSettlementId: number;
	targetX: number;
	targetY: number;
	state: NPCState;
	stateTimer: number;
	hp: number;
	maxHp: number;
	level: number;
	teleportCooldown: number;
	traits: NPCTrait[];
	inventory: Inventory;
	army: ArmyComposition;
	characterData: CharacterData;
};

// ── NPC Type Registry ───────────────────────────────────────────
// One config object per type. To add a new NPC type:
//   1. Add enum value to NPCType
//   2. Add an entry to NPC_TYPE_DEFS
//   3. If it needs new AI, add a function in npc-ai.ts
//   4. Done. No other files need changes.

export type ArmyGen = (level: number, rng: () => number) => ArmyComposition;

/** Appearance tweak applied after random character generation. */
type AppearanceFn = (character: CharacterData) => CharacterData;

export type NpcTypeDef = {
	names: string[];
	baseHp: number;
	baseLevel: number;
	/** AI behaviour function from npc-ai.ts. */
	ai: (npc: NPC, ctx: TickContext) => void;
	/** Optional army generator; defaults to empty army. */
	army?: ArmyGen;
	/** Optional appearance override. */
	appearance?: AppearanceFn;
	/** Subworld combat template — universal stats when this NPC fights. */
	combat: CombatTemplate;
};

// ── Appearance helpers ──

function withShoulderArmor(c: CharacterData): CharacterData {
	const u = {...c, sprites: {...c.sprites}, hidden: [...c.hidden]};
	if (u.sprites.ShoulderA === 0) {
		u.sprites.ShoulderA = 1;
	}

	return u;
}

function withBackpack(c: CharacterData): CharacterData {
	const u = {...c, sprites: {...c.sprites}, hidden: [...c.hidden]};
	if (u.sprites.BackA === 0) {
		u.sprites.BackA = 1;
	}

	return u;
}

function withHorns(c: CharacterData): CharacterData {
	const u = {...c, sprites: {...c.sprites}, hidden: [...c.hidden]};
	if (u.sprites.Horns === 0) {
		u.sprites.Horns = 1;
	}

	return u;
}

// ── Army generators ──

function guardArmy(level: number, rng: () => number): ArmyComposition {
	const a = defaultArmy();
	a[UnitType.Swordsman] = 1 + Math.floor(rng() * level);
	a[UnitType.Archer] = Math.floor(rng() * Math.max(1, level - 1));
	a[UnitType.Spearman] = Math.floor(rng() * 2);
	return a;
}

function banditArmy(level: number, rng: () => number): ArmyComposition {
	const a = defaultArmy();
	a[UnitType.Swordsman] = 1 + Math.floor(rng() * level * 0.5);
	a[UnitType.Archer] = Math.floor(rng() * level * 0.3);
	return a;
}

function caravanArmy(_level: number, _rng: () => number): ArmyComposition {
	const a = defaultArmy();
	a[UnitType.Swordsman] = 1;
	return a;
}

// ── Registry ──

export const NPC_TYPE_DEFS: Record<number, NpcTypeDef> = {
	[NPCType.Peasant]: {
		names: ['Ivan', 'Pyotr', 'Sergey', 'Dmitry', 'Alexei', 'Nikolai', 'Vasily', 'Grigory', 'Fedor', 'Andrei', 'Olga', 'Natalya', 'Katya', 'Masha', 'Dasha'],
		baseHp: 25, baseLevel: 1,
		ai: aiHomeWanderer,
		combat: {
			hp: 25, damage: 3, speed: 20, attackRange: 2, cooldown: 1.5, label: 'Psr',
		},
	},
	[NPCType.Woodcutter]: {
		names: ['Borislav', 'Timofey', 'Yegor', 'Luka', 'Matvey'],
		baseHp: 30, baseLevel: 1,
		ai: aiWoodcutter,
		combat: {
			hp: 30, damage: 8, speed: 20, attackRange: 2, cooldown: 1.2, label: 'Wdc',
		},
	},
	[NPCType.Merchant]: {
		names: ['Kartash', 'Bazukin', 'Torgin', 'Menkov', 'Skaldin'],
		baseHp: 30, baseLevel: 3,
		ai: aiTrader,
		appearance: withBackpack,
		combat: {
			hp: 30, damage: 5, speed: 25, attackRange: 2, cooldown: 1.5, label: 'Mrc',
		},
	},
	[NPCType.Caravan]: {
		names: ['Putnik', 'Dorozhkin', 'Obozov', 'Strannik', 'Koleso'],
		baseHp: 25, baseLevel: 2,
		ai: aiNomad,
		army: caravanArmy,
		appearance: withBackpack,
		combat: {
			hp: 25, damage: 4, speed: 30, attackRange: 2, cooldown: 1.5, label: 'Cvn',
		},
	},
	[NPCType.Bandit]: {
		names: ['Razboy', 'Diki', 'Grozny', 'Slyak', 'Khvat'],
		baseHp: 50, baseLevel: 2,
		ai: aiAggressive,
		army: banditArmy,
		combat: {
			hp: 50, damage: 12, speed: 45, attackRange: 3, cooldown: 1, label: 'Bnd',
		},
	},
	[NPCType.Guard]: {
		names: ['Strazhnik', 'Boyar', 'Vityaz', 'Desyatnik', 'Druzhina'],
		baseHp: 55, baseLevel: 3,
		ai: aiPatrol,
		army: guardArmy,
		appearance: withShoulderArmor,
		combat: {
			hp: 55, damage: 14, speed: 35, attackRange: 3, cooldown: 1, label: 'Grd',
		},
	},
	[NPCType.Witch]: {
		names: ['Yaga', 'Vedma', 'Znakharka', 'Koldunia', 'Volshebnitsa'],
		baseHp: 60, baseLevel: 5,
		ai: aiTeleporter,
		appearance: withHorns,
		combat: {
			hp: 60, damage: 18, speed: 30, attackRange: 20, cooldown: 2, label: 'Wtc',
		},
	},
	[NPCType.Sorceress]: {
		names: ['Charodejka', 'Zaklinatelnitsa', 'Mistika', 'Runara', 'Svetozara'],
		baseHp: 70, baseLevel: 6,
		ai: aiWanderer,
		appearance: withHorns,
		combat: {
			hp: 70, damage: 22, speed: 25, attackRange: 25, cooldown: 1.8, label: 'Src',
		},
	},
};

// ── Internal helpers ──

function pickName(rng: () => number, type: NPCType): string {
	const def = NPC_TYPE_DEFS[type];
	const pool = def?.names ?? NPC_TYPE_DEFS[NPCType.Peasant].names;
	return pool[Math.floor(rng() * pool.length)];
}

function generateNpcCharacter(type: NPCType): CharacterData {
	const character = CharacterManager.generateRandomCharacter(paletteManager.getDefaultPaletteState());
	const def = NPC_TYPE_DEFS[type];
	return def?.appearance ? def.appearance(character) : character;
}

function findValidSpawn(
	cx: number, cy: number,
	radius: number,
	rng: () => number,
	mapW: number, mapH: number,
	isLand: (x: number, y: number) => boolean,
	maxAttempts = 20,
): {x: number; y: number} {
	for (let attempt = 0; attempt < maxAttempts; attempt++) {
		const x = wrapCoord(cx + Math.floor(rng() * radius * 2) - radius, mapW);
		const y = wrapCoord(cy + Math.floor(rng() * radius * 2) - radius, mapH);
		if (isLand(x, y)) {
			return {x, y};
		}
	}

	return {x: cx, y: cy};
}

function makeNpc(
	id: number, type: NPCType, factionId: string, rng: () => number,
	x: number, y: number, homeId: number,
): NPC {
	const def = NPC_TYPE_DEFS[type] ?? NPC_TYPE_DEFS[NPCType.Peasant];
	const hp = def.baseHp + Math.floor(rng() * 15);
	const lvl = def.baseLevel + Math.floor(rng() * 4);

	const traitCount = 1 + Math.floor(rng() * 2);
	const traits: NPCTrait[] = [];
	for (let i = 0; i < traitCount; i++) {
		const t = ALL_TRAITS[Math.floor(rng() * ALL_TRAITS.length)];
		if (!traits.includes(t)) {
			traits.push(t);
		}
	}

	return {
		id,
		name: pickName(rng, type),
		type,
		factionId,
		x, y,
		visualX: x,
		visualY: y,
		visualSpeed: 0,
		homeSettlementId: homeId,
		targetSettlementId: -1,
		targetX: x,
		targetY: y,
		state: NPCState.Idle,
		stateTimer: 0,
		hp,
		maxHp: hp,
		level: lvl,
		teleportCooldown: 0,
		traits,
		inventory: (() => {
			const inv = createInventory();
			const items = generateNpcInventory(type, lvl, rng);
			for (const item of items) {
				inv.items.push(item);
			}

			return inv;
		})(),
		army: def.army ? def.army(lvl, rng) : defaultArmy(),
		characterData: generateNpcCharacter(type),
	};
}

// ── Spawning ──

export function spawnNPCs(
	settlements: Array<{id: number; x: number; y: number}>,
	seed: number,
	mapWidth = 1024,
	mapHeight = 1024,
	isLand?: (x: number, y: number) => boolean,
): NPC[] {
	const npcs: NPC[] = [];
	let idCounter = 0;

	const rng = xorshift32(seed + 7777);

	const checkLand = isLand ?? (() => true);

	for (const settlement of settlements) {
		const {x: sx, y: sy, id: sid} = settlement;

		const faction = settlementFaction(sx, sy, mapWidth, mapHeight);

		const peasantCount = 2 + Math.floor(rng() * 3);
		for (let i = 0; i < peasantCount; i++) {
			const pos = findValidSpawn(sx, sy, 10, rng, mapWidth, mapHeight, checkLand);
			npcs.push(makeNpc(idCounter++, NPCType.Peasant, faction, rng, pos.x, pos.y, sid));
		}

		const woodcutterCount = 1 + Math.floor(rng() * 2);
		for (let i = 0; i < woodcutterCount; i++) {
			const pos = findValidSpawn(sx, sy, 12, rng, mapWidth, mapHeight, checkLand);
			npcs.push(makeNpc(idCounter++, NPCType.Woodcutter, faction, rng, pos.x, pos.y, sid));
		}

		if (rng() > 0.4) {
			const pos = findValidSpawn(sx, sy, 4, rng, mapWidth, mapHeight, checkLand);
			npcs.push(makeNpc(idCounter++, NPCType.Merchant, 'timaert', rng, pos.x, pos.y, sid));
		}

		const guardCount = 1 + Math.floor(rng() * 2);
		for (let i = 0; i < guardCount; i++) {
			const pos = findValidSpawn(sx, sy, 6, rng, mapWidth, mapHeight, checkLand);
			npcs.push(makeNpc(idCounter++, NPCType.Guard, faction, rng, pos.x, pos.y, sid));
		}
	}

	const caravanCount = Math.max(1, Math.floor(settlements.length * 0.3));
	for (let i = 0; i < caravanCount; i++) {
		const home = settlements[Math.floor(rng() * settlements.length)];
		const pos = findValidSpawn(home.x, home.y, 8, rng, mapWidth, mapHeight, checkLand);
		npcs.push(makeNpc(idCounter++, NPCType.Caravan, 'timaert', rng, pos.x, pos.y, home.id));
	}

	const banditCount = Math.floor(settlements.length * 0.3) + 2;
	for (let i = 0; i < banditCount; i++) {
		const ref = settlements[Math.floor(rng() * settlements.length)];
		const angle = rng() * Math.PI * 2;
		const dist = 20 + Math.floor(rng() * 30);
		const cx = wrapCoord(ref.x + Math.round(Math.cos(angle) * dist), mapWidth);
		const cy = wrapCoord(ref.y + Math.round(Math.sin(angle) * dist), mapHeight);
		const pos = findValidSpawn(cx, cy, 15, rng, mapWidth, mapHeight, checkLand);
		const f = rng() > 0.2 ? 'cults' : '';
		npcs.push(makeNpc(idCounter++, NPCType.Bandit, f, rng, pos.x, pos.y, -1));
	}

	const witchCount = Math.max(1, Math.floor(settlements.length * 0.1));
	for (let i = 0; i < witchCount; i++) {
		const ref = settlements[Math.floor(rng() * settlements.length)];
		const angle = rng() * Math.PI * 2;
		const dist = 25 + Math.floor(rng() * 35);
		const cx = wrapCoord(ref.x + Math.round(Math.cos(angle) * dist), mapWidth);
		const cy = wrapCoord(ref.y + Math.round(Math.sin(angle) * dist), mapHeight);
		const pos = findValidSpawn(cx, cy, 15, rng, mapWidth, mapHeight, checkLand);
		const f = rng() > 0.3 ? 'magika' : 'cults';
		npcs.push(makeNpc(idCounter++, NPCType.Witch, f, rng, pos.x, pos.y, -1));
	}

	const sorceressCount = Math.max(1, Math.floor(settlements.length * 0.05));
	for (let i = 0; i < sorceressCount; i++) {
		const ref = settlements[Math.floor(rng() * settlements.length)];
		const angle = rng() * Math.PI * 2;
		const dist = 30 + Math.floor(rng() * 40);
		const cx = wrapCoord(ref.x + Math.round(Math.cos(angle) * dist), mapWidth);
		const cy = wrapCoord(ref.y + Math.round(Math.sin(angle) * dist), mapHeight);
		const pos = findValidSpawn(cx, cy, 15, rng, mapWidth, mapHeight, checkLand);
		const f = rng() > 0.5 ? 'magika' : 'cults';
		npcs.push(makeNpc(idCounter++, NPCType.Sorceress, f, rng, pos.x, pos.y, -1));
	}

	return npcs;
}

// ── AI Tick ──

export type TickContext = {
	mapWidth: number;
	mapHeight: number;
	isTraversable?: (x: number, y: number) => boolean;
	settlements: Array<{id: number; x: number; y: number}>;
	trees: Array<{x: number; y: number}>;
	playerX: number;
	playerY: number;
};

export function tickNPCs(npcs: NPC[], ctx: TickContext): void {
	for (const npc of npcs) {
		if (npc.hp <= 0) {
			continue;
		}

		const def = NPC_TYPE_DEFS[npc.type];
		if (def) {
			def.ai(npc, ctx);
		}
	}
}

/**
 * Deterministic spawning of city residents based on population (S).
 */
export function spawnCityNPCs(
	population: number,
	seed: number,
	grid: Uint8Array,
	width: number,
	height: number,
): NPC[] {
	const residents: NPC[] = [];
	const count = Math.min(250, Math.floor(Math.sqrt(population) * 1.5));

	const rng = xorshift32(seed + 555);

	const roadIndices: number[] = [];
	for (const [i, element] of grid.entries()) {
		if (element === 1) {
			roadIndices.push(i);
		}
	}

	if (roadIndices.length === 0) {
		return [];
	}

	for (let i = 0; i < count; i++) {
		const roadIdx = roadIndices[Math.floor(rng() * roadIndices.length)];
		const x = roadIdx % width;
		const y = Math.floor(roadIdx / width);

		const traitCount = 1 + Math.floor(rng() * 2);
		const traits: NPCTrait[] = [];
		for (let k = 0; k < traitCount; k++) {
			const t = ALL_TRAITS[Math.floor(rng() * ALL_TRAITS.length)];
			if (!traits.includes(t)) {
				traits.push(t);
			}
		}

		const faction = settlementFaction(x, y, width, height);

		residents.push({
			id: 10_000 + i,
			name: pickName(rng, NPCType.Peasant),
			type: NPCType.Peasant,
			factionId: faction,
			x, y,
			visualX: x,
			visualY: y,
			visualSpeed: 0,
			homeSettlementId: -1,
			targetSettlementId: -1,
			targetX: x,
			targetY: y,
			state: NPCState.Idle,
			stateTimer: Math.floor(rng() * 30),
			hp: 20,
			maxHp: 20,
			level: 1,
			teleportCooldown: 0,
			traits,
			inventory: createInventory(),
			army: defaultArmy(),
			characterData: generateNpcCharacter(NPCType.Peasant),
		});
	}

	return residents;
}

// ── Deserter spawning ──

/**
 * Spawn deserter NPCs near a position (from the global deserter pool).
 * They act like bandits with the 'cults' faction (or empty).
 * Returns new NPC array to be appended to the world NPC list.
 */
export function spawnDeserters(
	count: number,
	nearX: number,
	nearY: number,
	startId: number,
	mapWidth = 1024,
	mapHeight = 1024,
	isLand?: (x: number, y: number) => boolean,
): NPC[] {
	if (count <= 0) {
		return [];
	}

	const deserted: NPC[] = [];
	const rng = xorshift32(Math.trunc(nearX * 7919 + nearY * 6271 + count));

	const checkLand = isLand ?? (() => true);

	for (let i = 0; i < count; i++) {
		const angle = rng() * Math.PI * 2;
		const dist = 15 + Math.floor(rng() * 25);
		const cx = wrapCoord(nearX + Math.round(Math.cos(angle) * dist), mapWidth);
		const cy = wrapCoord(nearY + Math.round(Math.sin(angle) * dist), mapHeight);
		const pos = findValidSpawn(cx, cy, 10, rng, mapWidth, mapHeight, checkLand);
		deserted.push(makeNpc(startId + i, NPCType.Bandit, '', rng, pos.x, pos.y, -1));
	}

	return deserted;
}

// ── Faction helpers ──

/** Derive a settlement's faction from its position on the map. */
export function settlementFaction(
	sx: number, sy: number,
	mapWidth = 1024, mapHeight = 1024,
): string {
	const nx = sx / mapWidth;
	const ny = sy / mapHeight;
	if (ny < 0.3) {
		return nx < 0.5 ? 'magika' : 'barbarians';
	}

	if (ny > 0.7) {
		return 'timaert';
	}

	return 'empire';
}

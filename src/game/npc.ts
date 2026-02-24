// === NPC System ===
// Ported from old_concept: Peasant, Woodcutter, Merchant, Caravan, Bandit, Guard, Witch, Sorceress

import type {CharacterData} from '../character/types';
import {CharacterManager} from '../character/character-generator';
import {paletteManager} from '../character/palette';
import {type Inventory, createInventory, generateNpcInventory} from './items';

export {SPRITE_CITY} from './renderer';

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
	factionId: string; // empire, magika, barbarians, timaert, cults or ""
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
	characterData: CharacterData;
};

// ── Name pools ──

const PEASANT_NAMES = [
	'Ivan',
	'Pyotr',
	'Sergey',
	'Dmitry',
	'Alexei',
	'Nikolai',
	'Vasily',
	'Grigory',
	'Fedor',
	'Andrei',
	'Olga',
	'Natalya',
	'Katya',
	'Masha',
	'Dasha',
];

const WOODCUTTER_NAMES = [
	'Borislav', 'Timofey', 'Yegor', 'Luka', 'Matvey',
];

const MERCHANT_NAMES = [
	'Kartash', 'Bazukin', 'Torgin', 'Menkov', 'Skaldin',
];

const CARAVAN_NAMES = [
	'Putnik', 'Dorozhkin', 'Obozov', 'Strannik', 'Koleso',
];

const BANDIT_NAMES = [
	'Razboy', 'Diki', 'Grozny', 'Slyak', 'Khvat',
];

const GUARD_NAMES = [
	'Strazhnik', 'Boyar', 'Vityaz', 'Desyatnik', 'Druzhina',
];

const WITCH_NAMES = [
	'Yaga', 'Vedma', 'Znakharka', 'Koldunia', 'Volshebnitsa',
];

const SORCERESS_NAMES = [
	'Charodejka', 'Zaklinatelnitsa', 'Mistika', 'Runara', 'Svetozara',
];

const NAME_POOLS: Record<number, string[]> = {
	[NPCType.Peasant]: PEASANT_NAMES,
	[NPCType.Woodcutter]: WOODCUTTER_NAMES,
	[NPCType.Merchant]: MERCHANT_NAMES,
	[NPCType.Caravan]: CARAVAN_NAMES,
	[NPCType.Bandit]: BANDIT_NAMES,
	[NPCType.Guard]: GUARD_NAMES,
	[NPCType.Witch]: WITCH_NAMES,
	[NPCType.Sorceress]: SORCERESS_NAMES,
};

function pickName(rng: () => number, type: NPCType): string {
	const pool = NAME_POOLS[type] ?? PEASANT_NAMES;
	return pool[Math.floor(rng() * pool.length)];
}

// ── NPC type → appearance hints ──

function applyNpcTypeAppearance(character: CharacterData, type: NPCType): CharacterData {
	const updated = {...character, sprites: {...character.sprites}, hidden: [...character.hidden]};

	switch (type) {
		case NPCType.Guard: {
			// Always show shoulder armor
			if (updated.sprites.ShoulderA === 0) {
				updated.sprites.ShoulderA = 1;
			}

			break;
		}

		case NPCType.Merchant:
		case NPCType.Caravan: {
			// Always show back pack
			if (updated.sprites.BackA === 0) {
				updated.sprites.BackA = 1;
			}

			break;
		}

		case NPCType.Witch:
		case NPCType.Sorceress: {
			// Always show horns
			if (updated.sprites.Horns === 0) {
				updated.sprites.Horns = 1;
			}

			break;
		}

		case NPCType.Peasant:
		case NPCType.Woodcutter:
		case NPCType.Bandit: {
			break;
		}
	}

	return updated;
}

function generateNpcCharacter(type: NPCType): CharacterData {
	const character = CharacterManager.generateRandomCharacter(paletteManager.getDefaultPaletteState());
	return applyNpcTypeAppearance(character, type);
}

// ── Helpers ──

function wrapCoord(v: number, size: number): number {
	return ((v % size) + size) % size;
}

function torusDist(ax: number, ay: number, bx: number, by: number, w: number, h: number): number {
	let dx = Math.abs(ax - bx);
	let dy = Math.abs(ay - by);
	if (dx > w / 2) {
		dx = w - dx;
	}

	if (dy > h / 2) {
		dy = h - dy;
	}

	return Math.hypot(dx, dy);
}

function torusStepToward(
	fromX: number, fromY: number,
	toX: number, toY: number,
	w: number, h: number,
): {nx: number; ny: number} {
	let dx = toX - fromX;
	let dy = toY - fromY;
	// Handle torus wrap — pick shorter direction
	if (dx > w / 2) {
		dx -= w;
	} else if (dx < -w / 2) {
		dx += w;
	}

	if (dy > h / 2) {
		dy -= h;
	} else if (dy < -h / 2) {
		dy += h;
	}

	let nx = fromX;
	let ny = fromY;
	// Allow diagonal movement when both axes have distance
	if (dx !== 0) {
		nx += dx > 0 ? 1 : -1;
	}

	if (dy !== 0) {
		ny += dy > 0 ? 1 : -1;
	}

	return {nx: wrapCoord(nx, w), ny: wrapCoord(ny, h)};
}

// Find a valid (traversable, non-water) position near a center point
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

	// Fallback: use center (settlement is guaranteed on land)
	return {x: cx, y: cy};
}

function makeNpc(
	id: number, type: NPCType, factionId: string, rng: () => number,
	x: number, y: number, homeId: number,
): NPC {
	const hpBase: Record<number, number> = {
		[NPCType.Peasant]: 25,
		[NPCType.Woodcutter]: 30,
		[NPCType.Merchant]: 30,
		[NPCType.Caravan]: 25,
		[NPCType.Bandit]: 50,
		[NPCType.Guard]: 55,
		[NPCType.Witch]: 60,
		[NPCType.Sorceress]: 70,
	};
	const lvlBase: Record<number, number> = {
		[NPCType.Peasant]: 1,
		[NPCType.Woodcutter]: 1,
		[NPCType.Merchant]: 3,
		[NPCType.Caravan]: 2,
		[NPCType.Bandit]: 2,
		[NPCType.Guard]: 3,
		[NPCType.Witch]: 5,
		[NPCType.Sorceress]: 6,
	};
	const hp = (hpBase[type] ?? 30) + Math.floor(rng() * 15);
	const lvl = (lvlBase[type] ?? 1) + Math.floor(rng() * 4);
	
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

	let s = seed + 7777;
	const rng = (): number => {
		s = (s * 16_807 + 0) % 2_147_483_647;
		return s / 2_147_483_647;
	};

	// Default: always land (fallback if no checker provided)
	const checkLand = isLand ?? (() => true);

	for (const settlement of settlements) {
		const {x: sx, y: sy, id: sid} = settlement;
		
		// Географическое определение фракции поселения
		let settlementFaction = 'empire';
		if (sy < mapHeight * 0.3) {
			settlementFaction = sx < mapWidth * 0.5 ? 'magika' : 'barbarians';
		} else if (sy > mapHeight * 0.7) {
			settlementFaction = 'timaert';
		}

		// 2-4 Peasants per settlement
		const peasantCount = 2 + Math.floor(rng() * 3);
		for (let i = 0; i < peasantCount; i++) {
			const pos = findValidSpawn(sx, sy, 10, rng, mapWidth, mapHeight, checkLand);
			npcs.push(makeNpc(idCounter++, NPCType.Peasant, settlementFaction, rng, pos.x, pos.y, sid));
		}

		// 1-2 Woodcutters per settlement
		const woodcutterCount = 1 + Math.floor(rng() * 2);
		for (let i = 0; i < woodcutterCount; i++) {
			const pos = findValidSpawn(sx, sy, 12, rng, mapWidth, mapHeight, checkLand);
			npcs.push(makeNpc(idCounter++, NPCType.Woodcutter, settlementFaction, rng, pos.x, pos.y, sid));
		}

		// 0-1 Merchant per settlement (60% chance)
		if (rng() > 0.4) {
			const pos = findValidSpawn(sx, sy, 4, rng, mapWidth, mapHeight, checkLand);
			npcs.push(makeNpc(idCounter++, NPCType.Merchant, 'timaert', rng, pos.x, pos.y, sid));
		}

		// 1-2 Guards per settlement
		const guardCount = 1 + Math.floor(rng() * 2);
		for (let i = 0; i < guardCount; i++) {
			const pos = findValidSpawn(sx, sy, 6, rng, mapWidth, mapHeight, checkLand);
			npcs.push(makeNpc(idCounter++, NPCType.Guard, settlementFaction, rng, pos.x, pos.y, sid));
		}
	}

	// Caravans: ~30% of settlement count, spawn near a random settlement
	const caravanCount = Math.max(1, Math.floor(settlements.length * 0.3));
	for (let i = 0; i < caravanCount; i++) {
		const home = settlements[Math.floor(rng() * settlements.length)];
		const pos = findValidSpawn(home.x, home.y, 8, rng, mapWidth, mapHeight, checkLand);
		npcs.push(makeNpc(idCounter++, NPCType.Caravan, 'timaert', rng, pos.x, pos.y, home.id));
	}

	// Bandits: spawn away from settlements but on traversable land
	const banditCount = Math.floor(settlements.length * 0.3) + 2;
	for (let i = 0; i < banditCount; i++) {
		// Pick a random settlement then offset 20-50 tiles away
		const ref = settlements[Math.floor(rng() * settlements.length)];
		const angle = rng() * Math.PI * 2;
		const dist = 20 + Math.floor(rng() * 30);
		const cx = wrapCoord(ref.x + Math.round(Math.cos(angle) * dist), mapWidth);
		const cy = wrapCoord(ref.y + Math.round(Math.sin(angle) * dist), mapHeight);
		const pos = findValidSpawn(cx, cy, 15, rng, mapWidth, mapHeight, checkLand);
		const f = rng() > 0.2 ? 'cults' : '';
		npcs.push(makeNpc(idCounter++, NPCType.Bandit, f, rng, pos.x, pos.y, -1));
	}

	// Witches: ~10% of settlements, near forests (offset from settlements)
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

	// Sorceresses: rare, 1-2 total
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

// Tick interval must match the value in GameScreen
const TICK_SEC = 0.5;

function setNpcVisualSpeed(npc: NPC, oldX: number, oldY: number): void {
	const dist = Math.hypot(npc.x - oldX, npc.y - oldY);
	npc.visualSpeed = dist > 0 ? dist / TICK_SEC : 0;
}

function tryMove(
	npc: NPC, tx: number, ty: number,
	ctx: TickContext,
): boolean {
	const {nx, ny} = torusStepToward(npc.x, npc.y, tx, ty, ctx.mapWidth, ctx.mapHeight);
	const isDiagonal = nx !== npc.x && ny !== npc.y;
	const oldX = npc.x;
	const oldY = npc.y;

	if (!ctx.isTraversable || ctx.isTraversable(nx, ny)) {
		npc.x = nx;
		npc.y = ny;
		setNpcVisualSpeed(npc, oldX, oldY);
		return true;
	}

	// Diagonal blocked — fall back to cardinal axes
	if (isDiagonal) {
		if (!ctx.isTraversable || ctx.isTraversable(nx, npc.y)) {
			npc.x = nx;
			setNpcVisualSpeed(npc, oldX, oldY);
			return true;
		}

		if (!ctx.isTraversable || ctx.isTraversable(npc.x, ny)) {
			npc.y = ny;
			setNpcVisualSpeed(npc, oldX, oldY);
			return true;
		}
	}

	return false;
}

function atTarget(npc: NPC, ctx: TickContext): boolean {
	return torusDist(npc.x, npc.y, npc.targetX, npc.targetY, ctx.mapWidth, ctx.mapHeight) < 2;
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

	return ctx.settlements.find(s => s.id === npc.homeSettlementId);
}

// --- Per-type AI ---

function tickPeasant(npc: NPC, ctx: TickContext): void {
	// Wander near home settlement, return if too far
	const home = homePos(npc, ctx);
	if (!home) {
		return;
	}

	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			const dist = torusDist(npc.x, npc.y, home.x, home.y, ctx.mapWidth, ctx.mapHeight);
			if (dist > 20) {
				// Too far, return home
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

function tickWoodcutter(npc: NPC, ctx: TickContext): void {
	// Find nearest tree → travel → work → return home
	const home = homePos(npc, ctx);
	if (!home) {
		return;
	}

	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			// Find closest tree within range 30
			let bestDist = Infinity;
			let bestTree: {x: number; y: number} | undefined;
			for (const tree of ctx.trees) {
				const d = torusDist(npc.x, npc.y, tree.x, tree.y, ctx.mapWidth, ctx.mapHeight);
				if (d < bestDist && d < 30) {
					bestDist = d;
					bestTree = tree;
				}
			}

			if (bestTree) {
				npc.targetX = bestTree.x;
				npc.targetY = bestTree.y;
				npc.state = NPCState.Traveling;
			} else {
				// No tree found, wander near home
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
			// Done cutting, return home
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

function tickMerchant(npc: NPC, ctx: TickContext): void {
	// Travel to another settlement to trade, then return home
	const home = homePos(npc, ctx);
	if (!home) {
		return;
	}

	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			// Pick a different settlement to travel to
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
			// Arrived at target settlement, "trade" for a while
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
			// Done trading, return home
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

function tickCaravan(npc: NPC, ctx: TickContext): void {
	// Nomadic trader: travels between settlements endlessly (never returns "home")
	if (npc.state === NPCState.Idle) {
		npc.stateTimer--;
		if (npc.stateTimer <= 0) {
			// Pick next settlement to visit
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

function tickBandit(npc: NPC, ctx: TickContext): void {
	// Chase player if within 10 tiles, otherwise wander
	const distToPlayer = torusDist(npc.x, npc.y, ctx.playerX, ctx.playerY, ctx.mapWidth, ctx.mapHeight);

	if (distToPlayer < 10) {
		npc.state = NPCState.Chasing;
		npc.targetX = ctx.playerX;
		npc.targetY = ctx.playerY;
		tryMove(npc, npc.targetX, npc.targetY, ctx);
		return;
	}

	if (npc.state === NPCState.Chasing) {
		// Lost sight of player
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

function tickGuard(npc: NPC, ctx: TickContext): void {
	// Patrol near home settlement; return if strayed too far (>8 tiles)
	const home = homePos(npc, ctx);
	if (!home) {
		return;
	}

	const distHome = torusDist(npc.x, npc.y, home.x, home.y, ctx.mapWidth, ctx.mapHeight);

	if (distHome > 12) {
		// Too far from home, return
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

function tickWitch(npc: NPC, ctx: TickContext): void {
	// Wander + rare teleportation (ported from old_concept)
	if (npc.teleportCooldown > 0) {
		npc.teleportCooldown--;
	}

	// ~0.5% chance to teleport each tick if cooldown is 0
	if (npc.teleportCooldown <= 0 && Math.random() < 0.005) {
		const p = pickRandomNearby(npc.x, npc.y, 40, ctx.mapWidth, ctx.mapHeight);
		if (!ctx.isTraversable || ctx.isTraversable(p.x, p.y)) {
			npc.x = p.x;
			npc.y = p.y;
			npc.teleportCooldown = 50;
			npc.state = NPCState.Idle;
			npc.stateTimer = 10;
			return;
		}
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

function tickSorceress(npc: NPC, ctx: TickContext): void {
	// Simple random wanderer (ported from old_concept — minimal AI)
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

// Dispatch table for AI ticks — avoids switch overhead
const AI_DISPATCH: Record<number, (npc: NPC, ctx: TickContext) => void> = {
	[NPCType.Peasant]: tickPeasant,
	[NPCType.Woodcutter]: tickWoodcutter,
	[NPCType.Merchant]: tickMerchant,
	[NPCType.Caravan]: tickCaravan,
	[NPCType.Bandit]: tickBandit,
	[NPCType.Guard]: tickGuard,
	[NPCType.Witch]: tickWitch,
	[NPCType.Sorceress]: tickSorceress,
};
export function tickNPCs(npcs: NPC[], ctx: TickContext): void {
	for (const npc of npcs) {
		if (npc.hp <= 0) {
			continue;
		}

		const handler = AI_DISPATCH[npc.type];
		if (handler) {
			handler(npc, ctx);
		}
	}
}

/**
 * Deterministic spawning of city residents based on population (S)
 * Logic from Masum: "filling with walking sprites the number of inhabitants from S"
 */
export function spawnCityNPCs(
	population: number,
	seed: number,
	grid: Uint8Array,
	width: number,
	height: number,
): NPC[] {
	const residents: NPC[] = [];
	// Formula: more people = more visible NPCs, but with diminishing returns (sqrt)
	const count = Math.min(250, Math.floor(Math.sqrt(population) * 1.5));

	let s = seed + 555;
	const rng = (): number => {
		s = (s * 16_807 + 0) % 2_147_483_647;
		return s / 2_147_483_647;
	};

	// Find road tiles for spawning
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

		// Наследуем фракцию города для жителей (упрощенно по координатам)
		let cityFaction = 'empire';
		if (y < height * 0.3) {
			cityFaction = x < width * 0.5 ? 'magika' : 'barbarians';
		} else if (y > height * 0.7) {
			cityFaction = 'timaert';
		}

		residents.push({
			id: 10_000 + i, // High ID to avoid conflict with world NPCs
			name: pickName(rng, NPCType.Peasant), // Используем "честные" пулы имен из наработок
			type: NPCType.Peasant,
			factionId: cityFaction,
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
			characterData: generateNpcCharacter(NPCType.Peasant),
		});
	}

	return residents;
}

/**
 * Specialized tick for city NPCs - they stay on roads (TILE_ROAD = 1)
 */
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
				// Pick a nearby road tile to walk to
				const angle = Math.random() * Math.PI * 2;
				const dist = 5 + Math.random() * 10;
				const tx = Math.floor((npc.x + Math.cos(angle) * dist + width) % width);
				const ty = Math.floor((npc.y + Math.sin(angle) * dist + height) % height);

				// Only walk to road or grass, avoid walls/houses
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

			// Move only if not blocked; fall back to cardinal if diagonal blocked
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

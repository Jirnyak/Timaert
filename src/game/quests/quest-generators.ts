// === Procedural Quest Generators — context-aware quest factories ===
//
// Layer 4 (Plot Content). Pure data generators — no engine imports,
// no bus subscriptions. Each generator uses game context (economy,
// distance, difficulty) to produce quests with scaled rewards.
//
// Adding a new procedural quest type = one generator function + one
// entry in QUEST_GENERATORS. No engine changes.

import {
	type AnySettlement, type Settlement, type Village, type WorldTime, isCity,
} from '../state';
import {torusDist, wrapCoord} from '../torus';
import {RESOURCE_NAMES, RESOURCE_BASE_PRICE, type ResourceName} from '../economy';
import {NPCType} from '../npc';
import type {Quest} from './quest-types';

// ── Generation context ──

export type QuestGenContext = {
	settlement: AnySettlement;
	allSettlements: Settlement[];
	allVillages: Village[];
	worldTime: WorldTime;
	mapWidth: number;
	mapHeight: number;
	/** Seeded RNG — deterministic per settlement per day. */
	rng: () => number;
	/** Optional cell-info callback for enriched quest descriptions. */
	getBiomeName?: (x: number, y: number) => string;
};

// ── Generator registry ──

type QuestGeneratorFn = (ctx: QuestGenContext) => Quest | undefined;

const QUEST_GENERATORS: QuestGeneratorFn[] = [
	genDeliveryQuest,
	genVisitQuest,
	genDestroyQuest,
	genProtectQuest,
	genFetchQuest,
	genScoutQuest,
	genSanctuaryQuest,
];

// ── Public API ──

/**
 * Generate available quests for a settlement. Deterministic given seed.
 * Cities produce 2–4 quests; villages produce 1–2. At least 1 guaranteed.
 */
export function generateSettlementQuests(ctx: QuestGenContext): Quest[] {
	const maxQuests = isCity(ctx.settlement) ? 2 + Math.floor(ctx.rng() * 3) : 1 + Math.floor(ctx.rng() * 2);
	const quests: Quest[] = [];

	// Shuffle generators deterministically
	const order = shuffled(QUEST_GENERATORS.length, ctx.rng);

	for (const gi of order) {
		if (quests.length >= maxQuests) {
			break;
		}

		const quest = QUEST_GENERATORS[gi](ctx);
		if (quest) {
			quests.push(quest);
		}
	}

	// Guarantee at least 1 quest — fallback to visit
	if (quests.length === 0) {
		const fallback = genVisitQuest(ctx);
		if (fallback) {
			quests.push(fallback);
		}
	}

	return quests;
}

// ── Individual generators ──

/** Delivery quest — bring scarce resources. Economy-driven. */
function genDeliveryQuest(ctx: QuestGenContext): Quest | undefined {
	const {settlement, rng} = ctx;
	if (!isCity(settlement)) {
		return undefined;
	}

	// Find resource with highest price (= highest demand)
	let bestResource: ResourceName = RESOURCE_NAMES[0];
	let bestPrice = 0;
	for (const r of RESOURCE_NAMES) {
		const price = settlement.eco.prices[r] ?? 0;
		if (price > bestPrice) {
			bestPrice = price;
			bestResource = r;
		}
	}

	const baseQty = 3 + Math.floor(rng() * 8);
	const basePrice = RESOURCE_BASE_PRICE[bestResource];
	const goldReward = Math.round(baseQty * basePrice * (1.5 + rng()));
	const xpReward = Math.round(goldReward * 0.3);
	const difficulty = Math.min(10, Math.ceil(baseQty / 2));

	return {
		id: `q_proc_deliver_${settlement.id}_${ctx.worldTime.day}`,
		title: `Supply ${bestResource}`,
		description: `${settlement.name} urgently needs ${baseQty} units of ${bestResource}. The local market pays well above standard rates.`,
		category: 'procedural',
		giverSettlementId: settlement.id,
		objectives: [{
			type: 'deliver_items',
			itemId: `mat_${bestResource.toLowerCase()}`,
			quantity: baseQty,
			targetSettlementId: settlement.id,
			completed: false,
		}],
		rewards: [
			{type: 'gold', amount: goldReward},
			{type: 'xp', amount: xpReward},
			{type: 'reputation', faction: factionOf(settlement, ctx), delta: 5},
		],
		expireDay: ctx.worldTime.day + 30,
		difficulty,
	};
}

/** Visit quest — travel to a distant settlement. */
function genVisitQuest(ctx: QuestGenContext): Quest | undefined {
	const {settlement, allSettlements, rng, mapWidth, mapHeight} = ctx;

	// Pick a distant settlement
	const candidates = allSettlements.filter(s => s.id !== settlement.id);
	if (candidates.length === 0) {
		return undefined;
	}

	// Sort by distance, pick from far half
	candidates.sort((a, b) =>
		torusDist(settlement.x, settlement.y, b.x, b.y, mapWidth, mapHeight)
		- torusDist(settlement.x, settlement.y, a.x, a.y, mapWidth, mapHeight));
	const target = candidates[Math.floor(rng() * Math.ceil(candidates.length / 2))];
	const dist = torusDist(settlement.x, settlement.y, target.x, target.y, mapWidth, mapHeight);
	const distFactor = 1 + dist / (mapWidth * 0.25);
	const goldReward = Math.round(30 * distFactor + rng() * 20);
	const difficulty = Math.min(10, Math.ceil(distFactor * 2));
	const travelInfo = describeDestination(ctx, target.x, target.y);

	return {
		id: `q_proc_visit_${settlement.id}_${target.id}_${ctx.worldTime.day}`,
		title: `Envoy to ${target.name}`,
		description: `Deliver a sealed letter to the magistrate of ${target.name}. ${travelInfo}`,
		category: 'procedural',
		giverSettlementId: settlement.id,
		objectives: [{
			type: 'visit_cell',
			x: target.x,
			y: target.y,
			radius: 5,
			completed: false,
		}],
		rewards: [
			{type: 'gold', amount: goldReward},
			{type: 'xp', amount: Math.round(goldReward * 0.5)},
		],
		expireDay: ctx.worldTime.day + Math.max(14, Math.round(dist / 10)),
		difficulty,
	};
}

/** Destroy quest — eliminate hostiles near settlement. */
function genDestroyQuest(ctx: QuestGenContext): Quest | undefined {
	const {settlement, rng} = ctx;

	const count = 1 + Math.floor(rng() * 3);
	const level = 1 + Math.floor(rng() * 5);
	const goldReward = Math.round(count * level * 15 + rng() * 30);
	const difficulty = Math.min(10, count + level);

	// Zone: 20–60 units from settlement
	const angle = rng() * Math.PI * 2;
	const dist = 20 + rng() * 40;
	const zoneX = Math.round(settlement.x + Math.cos(angle) * dist);
	const zoneY = Math.round(settlement.y + Math.sin(angle) * dist);
	const travelInfo = describeDestination(ctx, zoneX, zoneY);

	return {
		id: `q_proc_destroy_${settlement.id}_${ctx.worldTime.day}`,
		title: 'Clear the Road',
		description: `Bandits have been terrorising travellers near ${settlement.name}. Eliminate ${count} of them. ${travelInfo}`,
		category: 'procedural',
		giverSettlementId: settlement.id,
		objectives: [{
			type: 'destroy_npc',
			npcType: NPCType.Bandit,
			count,
			killed: 0,
			zoneX,
			zoneY,
			zoneRadius: 30,
			completed: false,
		}],
		rewards: [
			{type: 'gold', amount: goldReward},
			{type: 'xp', amount: Math.round(goldReward * 0.6)},
			{type: 'reputation', faction: factionOf(settlement, ctx), delta: 8},
		],
		onAccept: [{
			tag: 'spawn_entity' as never,
			npcType: NPCType.Bandit,
			x: zoneX,
			y: zoneY,
			level,
		}],
		expireDay: ctx.worldTime.day + 20,
		difficulty,
	};
}

/** Protect quest — stay at village to defend against raiders. */
function genProtectQuest(ctx: QuestGenContext): Quest | undefined {
	const {settlement, rng} = ctx;

	// Only villages generate protect quests, and only poor ones
	if (isCity(settlement)) {
		return undefined;
	}

	const village = settlement;
	if (village.mood !== 'Tense' && village.mood !== 'Unrest' && village.mood !== 'Revolt' && rng() > 0.3) {
		return undefined;
	}

	const hoursRequired = 4 + Math.floor(rng() * 8);
	const goldReward = Math.round(40 + hoursRequired * 8 + rng() * 20);
	const difficulty = Math.min(10, Math.ceil(hoursRequired / 2));

	return {
		id: `q_proc_protect_${settlement.id}_${ctx.worldTime.day}`,
		title: `Defend ${settlement.name}`,
		description: `Raiders threaten ${settlement.name}. Stay and protect the villagers for ${hoursRequired} hours.`,
		category: 'procedural',
		giverSettlementId: settlement.id,
		objectives: [{
			type: 'wait_at',
			x: settlement.x,
			y: settlement.y,
			radius: 5,
			hoursRequired,
			hoursWaited: 0,
			completed: false,
		}],
		rewards: [
			{type: 'gold', amount: goldReward},
			{type: 'xp', amount: Math.round(goldReward * 0.4)},
			{type: 'reputation', faction: factionOf(settlement, ctx), delta: 10},
		],
		onAccept: [{
			tag: 'spawn_entity' as never,
			npcType: NPCType.Bandit,
			x: settlement.x + Math.round((rng() - 0.5) * 60),
			y: settlement.y + Math.round((rng() - 0.5) * 60),
			level: 2 + Math.floor(rng() * 3),
		}],
		expireDay: ctx.worldTime.day + 7,
		difficulty,
	};
}

/** Fetch quest — find item in nearby area. */
function genFetchQuest(ctx: QuestGenContext): Quest | undefined {
	const {settlement, rng} = ctx;

	const items = ['mat_herb', 'mat_iron', 'mat_wood'];
	const itemId = items[Math.floor(rng() * items.length)];
	const quantity = 2 + Math.floor(rng() * 5);
	const goldReward = Math.round(quantity * 12 + rng() * 15);
	const difficulty = Math.min(10, Math.ceil(quantity / 2));

	return {
		id: `q_proc_fetch_${settlement.id}_${ctx.worldTime.day}`,
		title: 'Gather Materials',
		description: `${settlement.name} needs ${quantity} ${itemId.replace('mat_', '')}. Gather them from the surrounding lands.`,
		category: 'procedural',
		giverSettlementId: settlement.id,
		objectives: [{
			type: 'deliver_items',
			itemId,
			quantity,
			targetSettlementId: settlement.id,
			completed: false,
		}],
		rewards: [
			{type: 'gold', amount: goldReward},
			{type: 'xp', amount: Math.round(goldReward * 0.3)},
		],
		expireDay: ctx.worldTime.day + 14,
		difficulty,
	};
}

/** Scout quest — visit location and return. Two objectives: go + comeback. */
function genScoutQuest(ctx: QuestGenContext): Quest | undefined {
	const {settlement, rng, mapWidth, mapHeight} = ctx;

	// Pick a random direction 30–80 units away
	const angle = rng() * Math.PI * 2;
	const dist = 30 + rng() * 50;
	const tx = wrapCoord(Math.round(settlement.x + Math.cos(angle) * dist), mapWidth);
	const ty = wrapCoord(Math.round(settlement.y + Math.sin(angle) * dist), mapHeight);
	const distFactor = 1 + dist / 50;
	const goldReward = Math.round(25 * distFactor + rng() * 15);
	const difficulty = Math.min(10, Math.ceil(distFactor * 1.5));
	const travelInfo = describeDestination(ctx, tx, ty);

	return {
		id: `q_proc_scout_${settlement.id}_${ctx.worldTime.day}`,
		title: 'Scout the Wilds',
		description: `Survey the area to the ${directionName(angle)} and report back to ${settlement.name}. ${travelInfo}`,
		category: 'procedural',
		giverSettlementId: settlement.id,
		objectives: [
			{
				type: 'visit_cell', x: tx, y: ty, radius: 8, completed: false,
			},
			{
				type: 'visit_cell', x: settlement.x, y: settlement.y, radius: 5, completed: false,
			},
		],
		rewards: [
			{type: 'gold', amount: goldReward},
			{type: 'xp', amount: Math.round(goldReward * 0.4)},
		],
		expireDay: ctx.worldTime.day + 21,
		difficulty,
	};
}

// ── Helpers ──

/** Build a human-readable travel blurb: "~N days to the [dir], in the [biome]." */
function describeDestination(ctx: QuestGenContext, targetX: number, targetY: number): string {
	const {settlement, mapWidth, mapHeight, getBiomeName} = ctx;
	const dist = torusDist(settlement.x, settlement.y, targetX, targetY, mapWidth, mapHeight);
	const days = Math.max(1, Math.round(dist / 10));
	const dx = targetX - settlement.x;
	const dy = targetY - settlement.y;
	const angle = Math.atan2(dy, dx);
	const dir = directionName(angle);
	const biome = getBiomeName?.(targetX, targetY);
	const biomePart = biome ? `, in the ${biome}` : '';
	return `~${days} day${days > 1 ? 's' : ''} travel to the ${dir}${biomePart}.`;
}

/** Sanctuary quest — villages only, find an ancient sanctuary cell. */
function genSanctuaryQuest(ctx: QuestGenContext): Quest | undefined {
	const {settlement, rng, mapWidth, mapHeight} = ctx;

	// Only villages offer this quest
	if (isCity(settlement)) {
		return undefined;
	}

	// 30% chance per day
	if (rng() > 0.3) {
		return undefined;
	}

	// Pick a random location 40–100 cells away
	const angle = rng() * Math.PI * 2;
	const dist = 40 + rng() * 60;
	const tx = wrapCoord(Math.round(settlement.x + Math.cos(angle) * dist), mapWidth);
	const ty = wrapCoord(Math.round(settlement.y + Math.sin(angle) * dist), mapHeight);
	const distFactor = 1 + dist / 50;
	const goldReward = Math.round(60 * distFactor + rng() * 40);
	const difficulty = Math.min(10, Math.ceil(distFactor * 2));
	const travelInfo = describeDestination(ctx, tx, ty);

	return {
		id: `q_proc_sanctuary_${settlement.id}_${ctx.worldTime.day}`,
		title: 'Find the Sanctuary',
		description: `An elder speaks of an ancient sanctuary lost to time. ${travelInfo}`,
		category: 'side',
		giverSettlementId: settlement.id,
		objectives: [{
			type: 'visit_cell',
			x: tx,
			y: ty,
			radius: 6,
			completed: false,
		}],
		rewards: [
			{type: 'gold', amount: goldReward},
			{type: 'xp', amount: Math.round(goldReward * 0.8)},
			{type: 'reputation', faction: factionOf(settlement, ctx), delta: 12},
		],
		expireDay: ctx.worldTime.day + Math.max(21, Math.round(dist / 8)),
		difficulty,
	};
}

function factionOf(_settlement: AnySettlement, _ctx: QuestGenContext): string {
	// Derive faction from settlement — simple lookup
	return 'empire'; // Read from faction ownership when available
}

function directionName(angle: number): string {
	const names = ['east', 'northeast', 'north', 'northwest', 'west', 'southwest', 'south', 'southeast'];
	return names[Math.round(((angle / (Math.PI * 2)) * 8 + 8) % 8)];
}

/** Fisher-Yates shuffle returning index array. */
function shuffled(n: number, rng: () => number): number[] {
	const array = Array.from({length: n}, (_, i) => i);
	for (let i = n - 1; i > 0; i--) {
		const j = Math.floor(rng() * (i + 1));
		[array[i], array[j]] = [array[j], array[i]];
	}

	return array;
}

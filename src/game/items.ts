// === Items & Inventory System ===

export const enum ItemType {
	Weapon = 0,
	Armor = 1,
	Potion = 2,
	Food = 3,
	Material = 4,
	Misc = 5,
}

export type Item = {
	id: string;
	name: string;
	type: ItemType;
	value: number; // Gold value
	quantity: number;
	icon: string; // Emoji icon for display
	description: string;
	// Optional stat bonuses when equipped/consumed
	effect?: {
		hp?: number;
		mp?: number;
		sp?: number;
		str?: number;
		end?: number;
		agi?: number;
	};
};

export type Inventory = {
	items: Item[];
	maxSlots: number; // Grid capacity
};

export function createInventory(maxSlots = 64): Inventory {
	return {items: [], maxSlots};
}

export function addItem(inv: Inventory, item: Item): boolean {
	// Try stacking with existing
	const existing = inv.items.find(i => i.id === item.id);
	if (existing) {
		existing.quantity += item.quantity;
		return true;
	}

	if (inv.items.length >= inv.maxSlots) {
		return false; // Inventory full
	}

	inv.items.push({...item});
	return true;
}

export function removeItem(inv: Inventory, itemId: string, quantity = 1): boolean {
	const idx = inv.items.findIndex(i => i.id === itemId);
	if (idx === -1) {
		return false;
	}

	const item = inv.items[idx];
	if (item.quantity < quantity) {
		return false;
	}

	item.quantity -= quantity;
	if (item.quantity <= 0) {
		inv.items.splice(idx, 1);
	}

	return true;
}

export function hasItem(inv: Inventory, itemId: string): boolean {
	return inv.items.some(i => i.id === itemId && i.quantity > 0);
}

export function itemCount(inv: Inventory): number {
	return inv.items.reduce((sum, i) => sum + i.quantity, 0);
}

// === Item Database ===

/**
 * Item blueprint — static definition. Quantity is set at creation time.
 * To add a new item: add one entry to ITEM_CATALOG. Everything else adapts.
 */
export type ItemDef = Omit<Item, 'quantity'>;

/** Central item catalog — single source of truth for all item definitions. */
export const ITEM_CATALOG: Record<string, ItemDef> = {
	// ── Currency / Resources ──
	gold: {
		id: 'gold', name: 'Gold', type: ItemType.Misc, value: 1,
		icon: '\u{1FA99}', description: 'Universal currency',
	},

	// ── Consumables ──
	potion_hp: {
		id: 'potion_hp', name: 'Health Potion', type: ItemType.Potion, value: 50,
		icon: '\u2764', description: 'Restores 30 HP', effect: {hp: 30},
	},
	potion_mp: {
		id: 'potion_mp', name: 'Mana Potion', type: ItemType.Potion, value: 75,
		icon: '\u2728', description: 'Restores 15 MP', effect: {mp: 15},
	},
	food_bread: {
		id: 'food_bread', name: 'Bread', type: ItemType.Food, value: 10,
		icon: '\u{1F35E}', description: 'Restores 10 HP', effect: {hp: 10},
	},

	// ── Materials ──
	mat_wood: {
		id: 'mat_wood', name: 'Wood', type: ItemType.Material, value: 5,
		icon: '\u{1FAB5}', description: 'Building material',
	},
	mat_iron: {
		id: 'mat_iron', name: 'Iron Ore', type: ItemType.Material, value: 15,
		icon: '\u26CF', description: 'Smithing material',
	},
	mat_herb: {
		id: 'mat_herb', name: 'Herb', type: ItemType.Material, value: 8,
		icon: '\u{1F33F}', description: 'Alchemy ingredient',
	},

	// ── Equipment ──
	wpn_dagger: {
		id: 'wpn_dagger', name: 'Rusty Dagger', type: ItemType.Weapon, value: 30,
		icon: '\u{1F5E1}', description: '+2 STR when equipped', effect: {str: 2},
	},
	arm_leather: {
		id: 'arm_leather', name: 'Leather Armor', type: ItemType.Armor, value: 60,
		icon: '\u{1F6E1}', description: '+2 END when equipped', effect: {end: 2},
	},

	// ── Valuables ──
	misc_gem: {
		id: 'misc_gem', name: 'Gemstone', type: ItemType.Misc, value: 100,
		icon: '\u{1F48E}', description: 'Valuable gem, can be sold',
	},
};

/** Create an item instance from the catalog by ID. */
export function makeItem(id: string, qty = 1): Item {
	const def = ITEM_CATALOG[id];
	if (!def) {
		return {
			id, name: id, type: ItemType.Misc, value: 0, quantity: qty,
			icon: '?', description: 'Unknown item',
		};
	}

	return {...def, quantity: qty};
}

// ── Legacy factory wrappers (backward-compatible, delegate to catalog) ──

export function makePotion(qty = 1): Item {
	return makeItem('potion_hp', qty);
}

export function makeMpPotion(qty = 1): Item {
	return makeItem('potion_mp', qty);
}

export function makeBread(qty = 1): Item {
	return makeItem('food_bread', qty);
}

export function makeWood(qty = 1): Item {
	return makeItem('mat_wood', qty);
}

export function makeIronOre(qty = 1): Item {
	return makeItem('mat_iron', qty);
}

export function makeRustyDagger(): Item {
	return makeItem('wpn_dagger');
}

export function makeLeatherArmor(): Item {
	return makeItem('arm_leather');
}

export function makeHerb(qty = 1): Item {
	return makeItem('mat_herb', qty);
}

export function makeGem(qty = 1): Item {
	return makeItem('misc_gem', qty);
}

/**
 * Loot table entry: item catalog key, chance (0–1), and quantity range.
 * Used by generateNpcInventory and generateSettlementInventory.
 */
type LootEntry = {
	item: string;
	chance: number;
	min: number;
	max: number;
	/** Minimum NPC level required (default 0). */
	minLevel?: number;
};

/** Loot tables keyed by NPC type number. */
const NPC_LOOT: Record<number, LootEntry[]> = {
	// Peasant
	0: [
		{
			item: 'food_bread', chance: 0.6, min: 1, max: 3,
		},
		{
			item: 'mat_wood', chance: 0.4, min: 1, max: 4,
		},
		{
			item: 'mat_herb', chance: 0.2, min: 1, max: 2,
		},
	],
	// Woodcutter
	1: [
		{
			item: 'mat_wood', chance: 1, min: 2, max: 7,
		},
		{
			item: 'food_bread', chance: 0.5, min: 1, max: 2,
		},
	],
	// Merchant
	2: [
		{
			item: 'potion_hp', chance: 0.7, min: 1, max: 3,
		},
		{
			item: 'food_bread', chance: 0.6, min: 2, max: 6,
		},
		{
			item: 'potion_mp', chance: 0.5, min: 1, max: 2,
		},
		{
			item: 'mat_iron', chance: 0.4, min: 1, max: 3,
		},
		{
			item: 'misc_gem', chance: 0.3, min: 1, max: 1,
		},
		{
			item: 'wpn_dagger', chance: 0.2, min: 1, max: 1,
		},
	],
	// Caravan
	3: [
		{
			item: 'food_bread', chance: 1, min: 3, max: 7,
		},
		{
			item: 'potion_hp', chance: 0.7, min: 1, max: 3,
		},
		{
			item: 'mat_iron', chance: 0.6, min: 2, max: 5,
		},
		{
			item: 'misc_gem', chance: 0.4, min: 1, max: 2,
		},
	],
	// Bandit
	4: [
		{
			item: 'potion_hp', chance: 0.7, min: 1, max: 2,
		},
		{
			item: 'wpn_dagger', chance: 0.5, min: 1, max: 1, minLevel: 3,
		},
		{
			item: 'misc_gem', chance: 0.4, min: 1, max: 2,
		},
	],
	// Guard
	5: [
		{
			item: 'food_bread', chance: 0.6, min: 1, max: 3,
		},
		{
			item: 'potion_hp', chance: 0.5, min: 1, max: 1,
		},
		{
			item: 'arm_leather', chance: 0.3, min: 1, max: 1, minLevel: 3,
		},
	],
	// Witch
	6: [
		{
			item: 'potion_mp', chance: 1, min: 1, max: 3,
		},
		{
			item: 'mat_herb', chance: 0.7, min: 2, max: 5,
		},
		{
			item: 'potion_hp', chance: 0.5, min: 1, max: 2,
		},
	],
	// Sorceress
	7: [
		{
			item: 'potion_mp', chance: 1, min: 2, max: 5,
		},
		{
			item: 'potion_hp', chance: 1, min: 1, max: 3,
		},
		{
			item: 'mat_herb', chance: 0.6, min: 3, max: 7,
		},
		{
			item: 'misc_gem', chance: 0.4, min: 1, max: 2,
		},
	],
};

function rollLoot(entries: LootEntry[], level: number, rng: () => number): Item[] {
	const items: Item[] = [];
	for (const entry of entries) {
		if (entry.minLevel && level < entry.minLevel) {
			continue;
		}

		if (rng() < entry.chance) {
			const qty = entry.min + Math.floor(rng() * (entry.max - entry.min + 1));
			items.push(makeItem(entry.item, qty));
		}
	}

	return items;
}

// Generate random loot for an NPC based on type and level
export function generateNpcInventory(npcType: number, npcLevel: number, rng: () => number): Item[] {
	const table = NPC_LOOT[npcType];
	if (!table) {
		return [];
	}

	return rollLoot(table, npcLevel, rng);
}

// ── Settlement loot tables (keyed by economy type) ──

const SETTLEMENT_BASE_LOOT: LootEntry[] = [
	{
		item: 'food_bread', chance: 1, min: 5, max: 14,
	},
	{
		item: 'potion_hp', chance: 1, min: 3, max: 9,
	},
];

const SETTLEMENT_ECONOMY_LOOT: Record<string, LootEntry[]> = {
	farming: [
		{
			item: 'food_bread', chance: 1, min: 10, max: 24,
		},
		{
			item: 'mat_herb', chance: 1, min: 5, max: 12,
		},
	],
	mining: [
		{
			item: 'mat_iron', chance: 1, min: 5, max: 14,
		},
		{
			item: 'misc_gem', chance: 1, min: 0, max: 2,
		},
	],
	trade: [
		{
			item: 'potion_hp', chance: 1, min: 5, max: 14,
		},
		{
			item: 'potion_mp', chance: 1, min: 3, max: 9,
		},
		{
			item: 'mat_iron', chance: 1, min: 3, max: 7,
		},
		{
			item: 'misc_gem', chance: 1, min: 0, max: 3,
		},
	],
	fishing: [
		{
			item: 'food_bread', chance: 1, min: 8, max: 19,
		},
		{
			item: 'mat_herb', chance: 1, min: 3, max: 7,
		},
	],
	crafting: [
		{
			item: 'mat_wood', chance: 1, min: 5, max: 14,
		},
		{
			item: 'mat_iron', chance: 1, min: 4, max: 11,
		},
	],
};

// Generate settlement inventory based on population and economy
export function generateSettlementInventory(population: number, economy: string, rng: () => number): Inventory {
	const inv = createInventory(); // Universal 64 slots

	// Base items
	for (const entry of SETTLEMENT_BASE_LOOT) {
		const qty = entry.min + Math.floor(rng() * (entry.max - entry.min + 1));
		if (qty > 0) {
			addItem(inv, makeItem(entry.item, qty));
		}
	}

	// Economy-based items
	const econLoot = SETTLEMENT_ECONOMY_LOOT[economy];
	if (econLoot) {
		for (const entry of econLoot) {
			const qty = entry.min + Math.floor(rng() * (entry.max - entry.min + 1));
			if (qty > 0) {
				addItem(inv, makeItem(entry.item, qty));
			}
		}
	}

	// Population-based bonus items
	const popTier = Math.floor(population / 200);
	if (popTier >= 1) {
		addItem(inv, makeItem('potion_mp', 1 + Math.floor(rng() * popTier)));
	}

	if (popTier >= 2) {
		addItem(inv, makeItem('misc_gem', Math.floor(rng() * popTier)));
	}

	return inv;
}

// Use a consumable item (potion/food) on the player
export function useItem(inv: Inventory, itemId: string, playerCombat: {currentHp: number; maxHp: number; currentMp: number; maxMp: number; currentSp: number; maxSp: number}): string | undefined {
	const item = inv.items.find(i => i.id === itemId);
	if (!item || item.quantity <= 0) {
		return undefined;
	}

	if (item.type !== ItemType.Potion && item.type !== ItemType.Food) {
		return undefined;
	}

	const messages: string[] = [];
	if (item.effect?.hp) {
		const healed = Math.min(item.effect.hp, playerCombat.maxHp - playerCombat.currentHp);
		playerCombat.currentHp += healed;
		messages.push(`+${healed} HP`);
	}

	if (item.effect?.mp) {
		const restored = Math.min(item.effect.mp, playerCombat.maxMp - playerCombat.currentMp);
		playerCombat.currentMp += restored;
		messages.push(`+${restored} MP`);
	}

	if (item.effect?.sp) {
		const restored = Math.min(item.effect.sp, playerCombat.maxSp - playerCombat.currentSp);
		playerCombat.currentSp += restored;
		messages.push(`+${restored} SP`);
	}

	removeItem(inv, itemId, 1);
	return messages.length > 0 ? `Used ${item.name}: ${messages.join(', ')}` : `Used ${item.name}`;
}

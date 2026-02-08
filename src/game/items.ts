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

export function createInventory(maxSlots = 24): Inventory {
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

export function makePotion(qty = 1): Item {
	return {
		id: 'potion_hp',
		name: 'Health Potion',
		type: ItemType.Potion,
		value: 50,
		quantity: qty,
		icon: '\u2764',
		description: 'Restores 30 HP',
		effect: {hp: 30},
	};
}

export function makeMpPotion(qty = 1): Item {
	return {
		id: 'potion_mp',
		name: 'Mana Potion',
		type: ItemType.Potion,
		value: 75,
		quantity: qty,
		icon: '\u2728',
		description: 'Restores 15 MP',
		effect: {mp: 15},
	};
}

export function makeBread(qty = 1): Item {
	return {
		id: 'food_bread',
		name: 'Bread',
		type: ItemType.Food,
		value: 10,
		quantity: qty,
		icon: '\u{1F35E}',
		description: 'Restores 10 HP',
		effect: {hp: 10},
	};
}

export function makeWood(qty = 1): Item {
	return {
		id: 'mat_wood',
		name: 'Wood',
		type: ItemType.Material,
		value: 5,
		quantity: qty,
		icon: '\u{1FAB5}',
		description: 'Building material',
	};
}

export function makeIronOre(qty = 1): Item {
	return {
		id: 'mat_iron',
		name: 'Iron Ore',
		type: ItemType.Material,
		value: 15,
		quantity: qty,
		icon: '\u26CF',
		description: 'Smithing material',
	};
}

export function makeRustyDagger(): Item {
	return {
		id: 'wpn_dagger',
		name: 'Rusty Dagger',
		type: ItemType.Weapon,
		value: 30,
		quantity: 1,
		icon: '\u{1F5E1}',
		description: '+2 STR when equipped',
		effect: {str: 2},
	};
}

export function makeLeatherArmor(): Item {
	return {
		id: 'arm_leather',
		name: 'Leather Armor',
		type: ItemType.Armor,
		value: 60,
		quantity: 1,
		icon: '\u{1F6E1}',
		description: '+2 END when equipped',
		effect: {end: 2},
	};
}

export function makeHerb(qty = 1): Item {
	return {
		id: 'mat_herb',
		name: 'Herb',
		type: ItemType.Material,
		value: 8,
		quantity: qty,
		icon: '\u{1F33F}',
		description: 'Alchemy ingredient',
	};
}

export function makeGem(qty = 1): Item {
	return {
		id: 'misc_gem',
		name: 'Gemstone',
		type: ItemType.Misc,
		value: 100,
		quantity: qty,
		icon: '\u{1F48E}',
		description: 'Valuable gem, can be sold',
	};
}

// Generate random loot for an NPC based on type and level
// NPCType values: Peasant=0, Woodcutter=1, Merchant=2, Caravan=3, Bandit=4, Guard=5, Witch=6, Sorceress=7
export function generateNpcInventory(npcType: number, npcLevel: number, rng: () => number): Item[] {
	const items: Item[] = [];

	// Peasants carry food and basic materials
	if (npcType === 0) {
		if (rng() > 0.4) {
			items.push(makeBread(1 + Math.floor(rng() * 3)));
		}

		if (rng() > 0.6) {
			items.push(makeWood(1 + Math.floor(rng() * 4)));
		}

		if (rng() > 0.8) {
			items.push(makeHerb(1 + Math.floor(rng() * 2)));
		}

		return items;
	}

	// Woodcutters carry wood and food
	if (npcType === 1) {
		items.push(makeWood(2 + Math.floor(rng() * 6)));
		if (rng() > 0.5) {
			items.push(makeBread(1 + Math.floor(rng() * 2)));
		}

		return items;
	}

	// Merchants carry trade goods
	if (npcType === 2) {
		if (rng() > 0.3) {
			items.push(makePotion(1 + Math.floor(rng() * 3)));
		}

		if (rng() > 0.4) {
			items.push(makeBread(2 + Math.floor(rng() * 5)));
		}

		if (rng() > 0.5) {
			items.push(makeMpPotion(1 + Math.floor(rng() * 2)));
		}

		if (rng() > 0.6) {
			items.push(makeIronOre(1 + Math.floor(rng() * 3)));
		}

		if (rng() > 0.7) {
			items.push(makeGem());
		}

		if (rng() > 0.8) {
			items.push(makeRustyDagger());
		}

		return items;
	}

	// Caravans carry bulk trade goods
	if (npcType === 3) {
		items.push(makeBread(3 + Math.floor(rng() * 5)));
		if (rng() > 0.3) {
			items.push(makePotion(1 + Math.floor(rng() * 3)));
		}

		if (rng() > 0.4) {
			items.push(makeIronOre(2 + Math.floor(rng() * 4)));
		}

		if (rng() > 0.6) {
			items.push(makeGem(1 + Math.floor(rng() * 2)));
		}

		return items;
	}

	// Bandits carry varied loot
	if (npcType === 4) {
		if (rng() > 0.3) {
			items.push(makePotion(Math.floor(rng() * 2) + 1));
		}

		if (rng() > 0.5 && npcLevel >= 3) {
			items.push(makeRustyDagger());
		}

		if (rng() > 0.6) {
			items.push(makeGem(Math.floor(rng() * 2) + 1));
		}

		return items;
	}

	// Guards carry weapons and rations
	if (npcType === 5) {
		if (rng() > 0.4) {
			items.push(makeBread(1 + Math.floor(rng() * 3)));
		}

		if (rng() > 0.5) {
			items.push(makePotion(1));
		}

		if (rng() > 0.7 && npcLevel >= 3) {
			items.push(makeLeatherArmor());
		}

		return items;
	}

	// Witches carry potions and herbs
	if (npcType === 6) {
		items.push(makeMpPotion(1 + Math.floor(rng() * 3)));
		if (rng() > 0.3) {
			items.push(makeHerb(2 + Math.floor(rng() * 4)));
		}

		if (rng() > 0.5) {
			items.push(makePotion(1 + Math.floor(rng() * 2)));
		}

		return items;
	}

	// Sorceresses carry powerful potions and rare items
	if (npcType === 7) {
		items.push(
			makeMpPotion(2 + Math.floor(rng() * 4)),
			makePotion(1 + Math.floor(rng() * 3)),
		);
		if (rng() > 0.4) {
			items.push(makeHerb(3 + Math.floor(rng() * 5)));
		}

		if (rng() > 0.6) {
			items.push(makeGem(1 + Math.floor(rng() * 2)));
		}

		return items;
	}

	return items;
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

// === RPG Attributes System (from old concept) ===

export type Attributes = {
	str: number; // Physical damage +1%, carry weight
	end: number; // HP regen +1%, HP
	agi: number; // Dodge, SP regen +1%
	wil: number; // MP regen +1%, MP
	int: number; // Spell damage +1%, spell slots
	wis: number; // EXP bonus +1%, learned spell slots
	lck: number; // Better loot, crit
	cha: number; // Trade discount, relation bonus
	spd: number; // Movement speed +1% (asymptotic), SP
};

export type CombatStats = {
	currentHp: number;
	maxHp: number;
	currentMp: number;
	maxMp: number;
	currentSp: number;
	maxSp: number;
	hpRegen: number;
	mpRegen: number;
	spRegen: number;
};

export type LevelData = {
	level: number;
	exp: number;
	expToNext: number;
	attributePoints: number;
	skillPoints: number;
	perkPoints: number;
};

export type DerivedBonuses = {
	physDamageMult: number;
	spellDamageMult: number;
	hpRegenMult: number;
	mpRegenMult: number;
	spRegenMult: number;
	expMult: number;
	moveSpeedMult: number;
	tradeDiscount: number;
	relationBonus: number;
	dodgeBase: number;
	critBase: number;
};

export type Skills = {
	bodybuilding: number;
	travel: number;
	fighter: number;
};

export type PerkID =
	| 'immortal'
	| 'shortLived'
	| 'mechanical'
	| 'talented'
	| 'gifted'
	| 'godsMark'
	| 'saint'
	| 'possess'
	| 'deathWord'
	| 'antimagus'
	| 'magicBody'
	| 'bloodMagic'
	| 'autist'
	| 'leader'
	| 'specialization'
	| 'generalist'
	| 'educated'
	| 'natural'
	| 'apostle'
	| 'demiurg'
	| 'revenant'
	| 'stonks'
	| 'sacrilegist'
	| 'kingPesant';

export type PerkInfo = {
	id: PerkID;
	name: string;
	description: string;
	advantage: string;
	disadvantage: string;
};

export type Perks = Set<PerkID>;

// All available perks
export const PERK_LIST: PerkInfo[] = [
	{
		id: 'immortal',
		name: 'Immortal',
		description: 'Never die from old age',
		advantage: 'Immunity to aging',
		disadvantage: '100% more EXP needed to level up',
	},
	{
		id: 'shortLived',
		name: 'Short-Lived',
		description: 'Die of old age at 33',
		advantage: '100% more EXP gained',
		disadvantage: 'Die at age 33',
	},
	{
		id: 'mechanical',
		name: 'Mechanical',
		description: 'Constructed being with no growth',
		advantage: 'Start with +100 attribute points & choose 10 skills',
		disadvantage: 'No level-up or EXP gain',
	},
	{
		id: 'talented',
		name: 'Talented',
		description: 'Natural prodigy',
		advantage: 'Instantly gain 1 level',
		disadvantage: 'Uses 1 perk point',
	},
	{
		id: 'gifted',
		name: 'Gifted',
		description: 'Extreme specialization',
		advantage: 'Choose two attributes; one is multiplied by 2',
		disadvantage: 'Other chosen attribute is divided by 2',
	},
	{
		id: 'natural',
		name: 'Natural',
		description: 'Pure physical excellence',
		advantage: '+1 attribute point per level',
		disadvantage: 'No skill points gained',
	},
	{
		id: 'educated',
		name: 'Educated',
		description: 'Highly trained specialist',
		advantage: '+1 skill point per level',
		disadvantage: 'No attribute points gained',
	},
];

export function defaultPerks(): Perks {
	return new Set();
}

export function hasPerk(perks: Perks, perkId: PerkID): boolean {
	return perks.has(perkId);
}

export function addPerk(perks: Perks, perkId: PerkID): void {
	perks.add(perkId);
}

export function removePerk(perks: Perks, perkId: PerkID): void {
	perks.delete(perkId);
}

export function defaultAttributes(): Attributes {
	return {
		str: 1, end: 1, agi: 1, wil: 1, int: 1, wis: 1, lck: 1, cha: 1, spd: 1,
	};
}

export function defaultSkills(): Skills {
	return {bodybuilding: 0, travel: 0, fighter: 0};
}

export function defaultLevelData(): LevelData {
	return {
		level: 1,
		exp: 0,
		expToNext: expToNextLevel(1),
		attributePoints: 9,
		skillPoints: 5,
		perkPoints: 1,
	};
}

// EXP_next(lvl) = 1000 * lvl * (0.1 * lvl + 1)
export function expToNextLevel(level: number): number {
	return Math.floor(1000 * level * (0.1 * level + 1));
}

// EXP_fight(lvl_m, k) = 10 * lvl_m * k
export function expFromFight(enemyLevel: number, modifier = 1): number {
	return Math.floor(10 * enemyLevel * modifier);
}

// EXP_quest(lvl_q, k) = 100 * lvl_q * k
export function expFromQuest(questLevel: number, modifier = 1): number {
	return Math.floor(100 * questLevel * modifier);
}

export function calculateCombatStats(
	attributes: Attributes,
	skills: Skills,
	baseHp = 100,
	baseMp = 10,
	baseSp = 100,
): CombatStats {
	const effectiveBaseHp = baseHp + skills.bodybuilding;
	const maxHp = Math.floor(effectiveBaseHp * (1 + 0.1 * attributes.end));
	const maxMp = Math.floor(baseMp * (1 + 0.1 * attributes.wil));
	const maxSp = Math.floor(baseSp * (1 + 0.1 * attributes.spd));

	return {
		currentHp: maxHp,
		maxHp,
		currentMp: maxMp,
		maxMp,
		currentSp: maxSp,
		maxSp,
		hpRegen: 1 * (1 + 0.01 * attributes.end),
		mpRegen: 0.5 * (1 + 0.01 * attributes.wil),
		spRegen: 2 * (1 + 0.01 * attributes.agi),
	};
}

export function calculateDerived(attributes: Attributes): DerivedBonuses {
	return {
		physDamageMult: 1 + attributes.str * 0.01,
		spellDamageMult: 1 + attributes.int * 0.01,
		hpRegenMult: 1 + attributes.end * 0.01,
		mpRegenMult: 1 + attributes.wil * 0.01,
		spRegenMult: 1 + attributes.agi * 0.01,
		expMult: 1 + attributes.wis * 0.01,
		moveSpeedMult: 1 + attributes.spd / (attributes.spd + 50),
		tradeDiscount: attributes.cha * 0.01,
		relationBonus: attributes.cha,
		dodgeBase: attributes.agi * 0.01,
		critBase: attributes.lck * 0.01,
	};
}

// Try to level up; returns true if leveled
export function tryLevelUp(levelData: LevelData): boolean {
	if (levelData.exp < levelData.expToNext) {
		return false;
	}

	levelData.exp -= levelData.expToNext;
	levelData.level += 1;
	levelData.expToNext = expToNextLevel(levelData.level);
	levelData.attributePoints += 1;
	levelData.skillPoints += 1;
	// Perk point every 3 levels
	if (levelData.level % 3 === 0) {
		levelData.perkPoints += 1;
	}

	return true;
}

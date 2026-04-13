// === RPG Attributes System (from old concept) ===

export type Attributes = {
	str: number; // +1 physical damage per point
	vit: number; // +10 max HP per point
	end: number; // +10 max SP per point
	wil: number; // +10 max MP per point
	int: number; // +1 spell damage per point
	wis: number; // +1% EXP bonus per point
	lck: number; // Crit scaling, better loot
	cha: number; // Trade discount, relation bonus
	spd: number; // Movement speed (asymptotic)
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
	rawPhysDamage: number; // Flat damage from STR
	rawSpellDamage: number; // Flat damage from INT
	expMult: number;
	moveSpeedMult: number;
	tradeDiscount: number;
	relationBonus: number;
	critBase: number;
};

export type Skills = {
	bodybuilding: number; // +5% max HP per rank
	meditation: number; // +5% max MP per rank
	travel: number; // +3% move speed, -2% terrain SP cost per rank
	fighter: number; // +5% physical damage per rank
	endurance: number; // +5% max SP per rank
	spellcraft: number; // +5% spell damage per rank
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
		str: 1, vit: 1, end: 1, wil: 1, int: 1, wis: 1, lck: 1, cha: 1, spd: 1,
	};
}

export function defaultSkills(): Skills {
	return {
		bodybuilding: 0, meditation: 0, travel: 0,
		fighter: 0, endurance: 0, spellcraft: 0,
	};
}

export function defaultLevelData(): LevelData {
	return {
		level: 1,
		exp: 0,
		expToNext: expToNextLevel(1),
		attributePoints: 8,
		skillPoints: 3,
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

// New formula: attributes give RAW bonuses, skills give MULTIPLIERS.
// FinalStat = (base + attrRaw) × (1 + skillRank × skillMult)
export function calculateCombatStats(
	attributes: Attributes,
	skills: Skills,
	baseHp = 100,
	baseMp = 100,
	baseSp = 100,
): CombatStats {
	// Raw from attributes
	const rawHp = baseHp + attributes.vit * 10;
	const rawMp = baseMp + attributes.wil * 10;
	const rawSp = baseSp + attributes.end * 10;

	// Skill multipliers
	const maxHp = Math.floor(rawHp * (1 + skills.bodybuilding * 0.05));
	const maxMp = Math.floor(rawMp * (1 + skills.meditation * 0.05));
	const maxSp = Math.floor(rawSp * (1 + skills.endurance * 0.05));

	return {
		currentHp: maxHp,
		maxHp,
		currentMp: maxMp,
		maxMp,
		currentSp: maxSp,
		maxSp,
		hpRegen: 10 * (1 + attributes.vit * 0.01),
		mpRegen: 10 * (1 + attributes.wil * 0.01),
		spRegen: 10 * (1 + attributes.end * 0.01),
	};
}

export function calculateDerived(attributes: Attributes, skills: Skills): DerivedBonuses {
	// Raw flat bonuses from attributes, multiplied by skills
	const rawPhys = attributes.str;
	const rawSpell = attributes.int;

	return {
		rawPhysDamage: rawPhys * (1 + skills.fighter * 0.05),
		rawSpellDamage: rawSpell * (1 + skills.spellcraft * 0.05),
		expMult: 1 + attributes.wis * 0.01,
		moveSpeedMult: (1 + attributes.spd / (attributes.spd + 50)) * (1 + skills.travel * 0.03),
		tradeDiscount: attributes.cha * 0.01,
		relationBonus: attributes.cha,
		critBase: attributes.lck / (attributes.lck + 50),
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
	levelData.attributePoints += 3;
	levelData.skillPoints += 1;
	// Perk point every 10 levels
	if (levelData.level % 10 === 0) {
		levelData.perkPoints += 1;
	}

	return true;
}

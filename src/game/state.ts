import {type LayerParameters, defaultParameters, type City} from '../webgl/webgl-context';
import type {CharacterData} from '../character/types';
import {CharacterManager} from '../character/character-generator';
import {paletteManager} from '../character/palette';
import {
	type Attributes,
	type CombatStats,
	type LevelData,
	type Skills,
	type Perks,
	type PerkID,
	defaultAttributes,
	defaultSkills,
	defaultLevelData,
	defaultPerks,
	calculateCombatStats,
} from './attributes';
import {
	type Inventory, createInventory, makePotion, makeBread, addItem, generateSettlementInventory,
} from './items';
import {FlagGenerator} from './flag-generator';
import {
	type ArmyComposition, UnitType, defaultArmy,
} from './army';
import {xorshift32} from './rng';

// === Factions ===
export type FactionId = 'empire' | 'magika' | 'barbarians' | 'timaert' | 'cults';

export type Faction = {
	id: FactionId;
	name: string;
	description: string;
	color: string;
	relations: Record<string, number>; // -100 (War) to 100 (Alliance)
};

// === Settlement info ===
export type SettlementMood = 'Prosperous' | 'Stable' | 'Tense' | 'Unrest' | 'Revolt';

export type SettlementHistory = {
	days: number[];
	population: number[];
};

export type Settlement = {
	id: number;
	name: string;
	x: number; // Pixel x on map
	y: number; // Pixel y on map
	population: number;
	economy: string;
	mood: SettlementMood;
	banner: string; // Data URL of the procedural flag
	inventory: Inventory; // Settlement's inventory for trading
	history: SettlementHistory;
	/** Locally raised militia available for hire. Generated from population. */
	garrison: ArmyComposition;
};

// === Player state ===
export type Reputation = Record<string, number>;

export type LogType = 'combat' | 'economy' | 'politics' | 'world';

export type LogEntry = {
	type: LogType;
	message: string;
	day: number;
};

export type PlayerState = {
	x: number; // Pixel x on map
	y: number; // Pixel y on map
	gold: number;
	items: number;
	currentSettlement: string | undefined; // Settlement name if in one
	attributes: Attributes;
	combatStats: CombatStats;
	levelData: LevelData;
	skills: Skills;
	perks: Perks;
	inventory: Inventory;
	reputation: Reputation;
	army: ArmyComposition;
	characterData: CharacterData;
	codexUnlocked: string[];
	eventLog: LogEntry[];
};

// === World time ===
export type WorldTime = {
	day: number;
	hour: number;
	minute: number;
};

// === Nested game sub-states ===
export type GameSubState =
	| {type: 'exploring'}
	| {type: 'paused'}
	| {type: 'trading'; settlementId: number}
	| {type: 'viewing_map'}
	| {type: 'event'; eventId: string}
	| {type: 'battle'; enemyId: string};

// === Full game state (serializable) ===
export type GameState = {
	version: number;
	saveName: string;
	savedAt: string; // ISO date string
	mapParams: LayerParameters;
	settlements: Settlement[];
	factions: Record<string, Faction>;
	player: PlayerState;
	worldTime: WorldTime;
	subState: GameSubState;
	seed: number;
	/** Global pool of fired/deserted soldiers (just counts, no entities). */
	deserterPool: ArmyComposition;
};

// === App-level screen routing ===
export type AppScreen =
	| {type: 'title'}
	| {type: 'load'}
	| {type: 'sandbox_setup'}
	| {type: 'game'; state: GameState};

// === Save/Load helpers ===
const SAVE_PREFIX = 'samosbor_save_';

export function listSaves(): Array<{key: string; name: string; savedAt: string}> {
	const saves: Array<{key: string; name: string; savedAt: string}> = [];
	for (let i = 0; i < localStorage.length; i++) {
		const key = localStorage.key(i);
		if (key?.startsWith(SAVE_PREFIX)) {
			try {
				const raw = localStorage.getItem(key);
				if (raw) {
					const parsed = JSON.parse(raw) as GameState;
					saves.push({
						key,
						name: parsed.saveName,
						savedAt: parsed.savedAt,
					});
				}
			} catch {
				// Skip corrupted saves
			}
		}
	}

	saves.sort((a, b) => b.savedAt.localeCompare(a.savedAt));
	return saves;
}

export function saveGame(state: GameState): string {
	const updated = {
		...state,
		savedAt: new Date().toISOString(),
		player: {
			...state.player,
			perks: [...state.player.perks] as unknown as Perks,
		},
	};
	const key = SAVE_PREFIX + Date.now().toString(36);
	localStorage.setItem(key, JSON.stringify(updated));
	return key;
}

export function loadGame(key: string): GameState | undefined {
	const raw = localStorage.getItem(key);
	if (!raw) {
		return undefined;
	}

	try {
		const parsed = JSON.parse(raw) as GameState;
		// Restore perks Set from serialized array
		if (Array.isArray(parsed.player.perks)) {
			parsed.player.perks = new Set(parsed.player.perks as unknown as PerkID[]);
		} else if (!(parsed.player.perks instanceof Set)) {
			parsed.player.perks = new Set();
		}

		// Migrate: add army if missing (old saves)
		if (!parsed.player.army) {
			const a = defaultArmy();
			a[UnitType.Swordsman] = 3;
			a[UnitType.Archer] = 2;
			a[UnitType.Spearman] = 1;
			(parsed.player as any).army = a;
		}

		// Migrate old named-field armies to Record<UnitType, number>
		const pa = parsed.player.army as any;
		if (pa.swordsmen !== undefined) {
			const migrated = defaultArmy();
			migrated[UnitType.Swordsman] = pa.swordsmen ?? 0;
			migrated[UnitType.Archer] = pa.archers ?? 0;
			migrated[UnitType.Spearman] = pa.spearmen ?? 0;
			migrated[UnitType.Horseman] = pa.horsemen ?? 0;
			parsed.player.army = migrated;
		}

		// Migrate: add garrison to settlements if missing
		for (const s of parsed.settlements) {
			if (!s.garrison) {
				(s as any).garrison = defaultArmy();
			}
		}

		// Migrate: add deserterPool if missing
		if (!(parsed as any).deserterPool) {
			(parsed as any).deserterPool = defaultArmy();
		}

		return parsed;
	} catch {
		return undefined;
	}
}

export function deleteSave(key: string): void {
	localStorage.removeItem(key);
}

// === Settlement name generator ===
const PREFIXES = [
	'Kras',
	'Bel',
	'Nov',
	'Star',
	'Vel',
	'Cher',
	'Zele',
	'Dol',
	'Gor',
	'Kame',
	'Dub',
	'Ber',
	'Sos',
	'Lip',
	'Ked',
	'Vol',
	'Don',
	'Ural',
	'Tver',
	'Ryb',
	'Smo',
	'Psk',
	'Kur',
	'Orel',
	'Tul',
	'Pen',
	'Tam',
];
const SUFFIXES = [
	'ovo',
	'ino',
	'sk',
	'burg',
	'grad',
	'gorod',
	'pole',
	'gorsk',
	'insk',
	'ovka',
	'inka',
	'ichi',
	'esti',
	'any',
	'uki',
	'ets',
];

function generateSettlementName(rng: () => number): string {
	const prefix = PREFIXES[Math.floor(rng() * PREFIXES.length)];
	const suffix = SUFFIXES[Math.floor(rng() * SUFFIXES.length)];
	return prefix + suffix;
}

function createStarterInventory(): Inventory {
	const inv = createInventory(); // Universal 64 slots
	addItem(inv, makePotion(2));
	addItem(inv, makeBread(5));
	return inv;
}

function starterArmy(): ArmyComposition {
	const a = defaultArmy();
	a[UnitType.Swordsman] = 3;
	a[UnitType.Archer] = 2;
	a[UnitType.Spearman] = 1;
	return a;
}

function createFactions(): Record<string, Faction> {
	return {
		empire: {
			id: 'empire', name: 'Empire of Light', color: '#fbbf24',
			description: 'Theocratic empire. Magic is forbidden.',
			relations: {magika: -80, cults: -100, timaert: 20},
		},
		magika: {
			id: 'magika', name: 'Magocracy', color: '#a78bfa',
			description: 'Ruled by powerful mages. High magic economy.',
			relations: {empire: -80, barbarians: -40, timaert: 10},
		},
		barbarians: {
			id: 'barbarians', name: 'Barbarian Kings', color: '#ef4444',
			description: 'Feudal lords ruling by might and steel.',
			relations: {empire: -20, magika: -40, cults: 10},
		},
		timaert: {
			id: 'timaert', name: 'Republic of Timaert', color: '#3b82f6',
			description: 'Maritime trade republic. Neutral and wealthy.',
			relations: {empire: 20, magika: 10, barbarians: 0},
		},
		cults: {
			id: 'cults', name: 'Black Cults', color: '#581c87',
			description: 'Worshippers of void and dead gods.',
			relations: {empire: -100, magika: -50, barbarians: 10},
		},
	};
}

// === Create initial game state from map params and cities ===
export function createGameState(
	mapParameters: LayerParameters,
	cities: City[],
	mapWidth: number,
	mapHeight: number,
): GameState {
	const rng = xorshift32(mapParameters.seed + 999);

	const settlements: Settlement[] = cities.map((city, i) => {
		const settlementSeed = mapParameters.seed + i * 555;
		const flagGen = new FlagGenerator(settlementSeed);
		const banner = flagGen.generate().toDataURL();

		const population = Math.floor(rng() * 900) + 100;
		const economy = ['farming', 'mining', 'trade', 'fishing', 'crafting'][Math.floor(rng() * 5)];
		const mood: SettlementMood = (['Prosperous', 'Stable', 'Tense', 'Unrest', 'Revolt'] as const)[Math.floor(rng() * 5)];

		// Create seeded RNG for this settlement's inventory
		const settlementRng = xorshift32(settlementSeed + 1000);
		const inventory = generateSettlementInventory(population, economy, settlementRng);

		return {
			id: i,
			name: generateSettlementName(rng),
			x: Math.floor(city.x * mapWidth),
			y: Math.floor(city.y * mapHeight),
			population,
			economy,
			mood,
			banner,
			inventory,
			history: {days: [], population: []},
			garrison: defaultArmy(),
		};
	});

	// Spawn player at a random settlement
	const spawnIdx = Math.floor(rng() * settlements.length);
	const spawn = settlements[spawnIdx];

	const attrs = defaultAttributes();
	const skills = defaultSkills();
	const combat = calculateCombatStats(attrs, skills);

	return {
		version: 1,
		saveName: 'Autosave',
		savedAt: new Date().toISOString(),
		mapParams: mapParameters,
		settlements,
		factions: createFactions(),
		player: {
			x: spawn.x,
			y: spawn.y,
			gold: 1000,
			items: 0,
			currentSettlement: spawn.name,
			attributes: attrs,
			combatStats: combat,
			levelData: defaultLevelData(),
			skills,
			perks: defaultPerks(),
			inventory: createStarterInventory(),
			reputation: {
				empire: 0, magika: 0, barbarians: 0, timaert: 0, cults: -10, Wilderness: 0,
			},
			army: starterArmy(),
			characterData: CharacterManager.generateRandomCharacter(paletteManager.getDefaultPaletteState()),
			codexUnlocked: ['cosmology', 'attributes', 'perks_skills', 'market', 'settlements'],
			eventLog: [],
		},
		worldTime: {day: 1, hour: 8, minute: 0},
		subState: {type: 'exploring'},
		seed: mapParameters.seed,
		deserterPool: defaultArmy(),
	};
}

export function createRandomGameState(): GameState {
	const seed = Math.floor(Math.random() * 100_000);
	const parameters: LayerParameters = {...defaultParameters, seed};
	// Cities will be generated by MapGenerator; we pass empty array initially
	// The GameScreen will populate settlements after map generation
	const attrs = defaultAttributes();
	const skills = defaultSkills();
	const combat = calculateCombatStats(attrs, skills);

	return {
		version: 1,
		saveName: 'New Game',
		savedAt: new Date().toISOString(),
		mapParams: parameters,
		settlements: [],
		factions: createFactions(),
		player: {
			x: 0,
			y: 0,
			gold: 1000,
			items: 0,
			currentSettlement: undefined,
			attributes: attrs,
			combatStats: combat,
			levelData: defaultLevelData(),
			skills,
			perks: defaultPerks(),
			inventory: createStarterInventory(),
			reputation: {
				empire: 0, magika: 0, barbarians: 0, timaert: 0, cults: -10, Wilderness: 0,
			},
			army: starterArmy(),
			characterData: CharacterManager.generateRandomCharacter(paletteManager.getDefaultPaletteState()),
			codexUnlocked: ['cosmology', 'attributes', 'perks_skills', 'market', 'settlements'],
			eventLog: [],
		},
		worldTime: {day: 1, hour: 8, minute: 0},
		subState: {type: 'exploring'},
		seed,
		deserterPool: defaultArmy(),
	};
}

export {defaultParameters as defaultParams, type LayerParameters as LayerParams} from '../webgl/webgl-context';

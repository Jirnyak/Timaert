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

// === Settlement info ===
export type SettlementMood = 'Prosperous' | 'Stable' | 'Tense' | 'Unrest' | 'Revolt';

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
};

// === Player state ===
export type Reputation = Record<string, number>;

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
	characterData: CharacterData;
	codexUnlocked: string[];
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
	player: PlayerState;
	worldTime: WorldTime;
	subState: GameSubState;
	seed: number;
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
		return JSON.parse(raw) as GameState;
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

function seededRandom(seed: number): () => number {
	let s = seed;
	return () => {
		s = (s * 1_103_515_245 + 12_345) & 0x7F_FF_FF_FF;
		return s / 0x7F_FF_FF_FF;
	};
}

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

// === Create initial game state from map params and cities ===
export function createGameState(
	mapParameters: LayerParameters,
	cities: City[],
	mapWidth: number,
	mapHeight: number,
): GameState {
	const rng = seededRandom(mapParameters.seed + 999);

	const settlements: Settlement[] = cities.map((city, i) => {
		const settlementSeed = mapParameters.seed + i * 555;
		const flagGen = new FlagGenerator(settlementSeed);
		const banner = flagGen.generate().toDataURL();

		const population = Math.floor(rng() * 900) + 100;
		const economy = ['farming', 'mining', 'trade', 'fishing', 'crafting'][Math.floor(rng() * 5)];
		const mood: SettlementMood = (['Prosperous', 'Stable', 'Tense', 'Unrest', 'Revolt'] as const)[Math.floor(rng() * 5)];

		// Create seeded RNG for this settlement's inventory
		const settlementRng = seededRandom(settlementSeed + 1000);
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
			reputation: {Wilderness: 0},
			characterData: CharacterManager.generateRandomCharacter(paletteManager.getDefaultPaletteState()),
			codexUnlocked: ['cosmology', 'attributes', 'perks_skills', 'market', 'settlements'],
		},
		worldTime: {day: 1, hour: 8, minute: 0},
		subState: {type: 'exploring'},
		seed: mapParameters.seed,
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
			reputation: {Wilderness: 0},
			characterData: CharacterManager.generateRandomCharacter(paletteManager.getDefaultPaletteState()),
			codexUnlocked: ['cosmology', 'attributes', 'perks_skills', 'market', 'settlements'],
		},
		worldTime: {day: 1, hour: 8, minute: 0},
		subState: {type: 'exploring'},
		seed,
	};
}

export {defaultParameters as defaultParams, type LayerParameters as LayerParams} from '../webgl/webgl-context';

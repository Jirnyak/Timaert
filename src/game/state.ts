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
	type Inventory, createInventory, makeItem, addItem, generateSettlementInventory,
} from './items';
import {FlagGenerator} from './flag-generator';
import {
	type ArmyComposition, UnitType, defaultArmy,
} from './army';
import {type SpellBook, createSpellBook, learnSpell} from './spells';
import {xorshift32} from './rng';
import {
	type EconomyState, type TradeRoute,
	createEconomyState, resourcesForTerrain,
} from './economy';

// === Factions ===
export type FactionId = 'empire' | 'magika' | 'barbarians' | 'timaert' | 'cults' | 'wildlife' | 'monsters';

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
	/** Local economy state (resources, goods, prices). */
	eco: EconomyState;
};

// === Village (resource-gathering landmark) ===

export type Village = {
	id: number;
	name: string;
	x: number;
	y: number;
	population: number;
	mood: SettlementMood;
	banner: string;
	inventory: Inventory;
	history: SettlementHistory;
	/** Local economy state with localResources for gathering. */
	eco: EconomyState;
	/** Nearest city this village trades with (settlement id). */
	nearestCityId: number;
	/** Day of last trade dispatch. */
	lastTradeDay: number;
};

/** Any settlement-like entity (city or village). */
export type AnySettlement = Settlement | Village;

/** Type guard: true for cities (have garrison + economy string). */
export function isCity(s: AnySettlement): s is Settlement {
	return 'garrison' in s;
}

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
	spellBook: SpellBook;
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

/** Bump this to invalidate all existing saves. */
export const kSaveVersion = 3;

// === Full game state (serializable) ===
export type GameState = {
	version: number;
	saveName: string;
	savedAt: string; // ISO date string
	mapParams: LayerParameters;
	settlements: Settlement[];
	villages: Village[];
	factions: Record<string, Faction>;
	player: PlayerState;
	worldTime: WorldTime;
	subState: GameSubState;
	seed: number;
	/** Global pool of fired/deserted soldiers (just counts, no entities). */
	deserterPool: ArmyComposition;
	/** Active trade routes in transit (caravans/peasant traders). */
	activeTradeRoutes: TradeRoute[];
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

		// Reject saves from older versions — no migration needed in early dev
		if (parsed.version !== kSaveVersion) {
			return undefined;
		}

		// Restore perks Set from serialized array
		if (Array.isArray(parsed.player.perks)) {
			parsed.player.perks = new Set(parsed.player.perks as unknown as PerkID[]);
		} else if (!(parsed.player.perks instanceof Set)) {
			parsed.player.perks = new Set();
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

const VILLAGE_SUFFIXES = [
	'ovka',
	'inka',
	'ichi',
	'esti',
	'any',
	'uki',
	'ets',
	'ino',
	'ovo',
	'yata',
	'ata',
	'eno',
];

function generateVillageName(rng: () => number): string {
	const prefix = PREFIXES[Math.floor(rng() * PREFIXES.length)];
	const suffix = VILLAGE_SUFFIXES[Math.floor(rng() * VILLAGE_SUFFIXES.length)];
	return prefix + suffix;
}

/**
 * Spawn villages around cities. 3-5 villages per city.
 * Prefers positions on or near existing roads between cities.
 * If `roadMask` is provided, candidates within 3 tiles of a road cell
 * are strongly preferred.
 */
export function generateVillages(
	settlements: Settlement[],
	seed: number,
	mapWidth: number,
	mapHeight: number,
	isLand: (x: number, y: number) => boolean,
	getHeight: (x: number, y: number) => number,
	roadMask?: Uint8Array,
): Village[] {
	const rng = xorshift32(seed + 3333);
	const villages: Village[] = [];
	let idCounter = 0;

	for (const city of settlements) {
		const count = 3 + Math.floor(rng() * 3); // 3-5 villages per city
		for (let v = 0; v < count; v++) {
			let px = 0;
			let py = 0;
			let placed = false;

			// First pass: try to place on/near a road (up to 20 attempts)
			if (roadMask) {
				for (let attempt = 0; attempt < 20; attempt++) {
					const angle = rng() * Math.PI * 2;
					const dist = 15 + Math.floor(rng() * 25);
					const cx = ((city.x + Math.round(Math.cos(angle) * dist)) % mapWidth + mapWidth) % mapWidth;
					const cy = ((city.y + Math.round(Math.sin(angle) * dist)) % mapHeight + mapHeight) % mapHeight;
					if (!isLand(cx, cy) || isTooClose_(cx, cy, settlements, villages, mapWidth, mapHeight)) {
						continue;
					}

					// Snap to nearest road cell within 3 tiles
					const snapped = snapToRoad_(cx, cy, roadMask, mapWidth, mapHeight, 3);
					if (snapped && isLand(snapped.x, snapped.y)
						&& !isTooClose_(snapped.x, snapped.y, settlements, villages, mapWidth, mapHeight)) {
						px = snapped.x;
						py = snapped.y;
						placed = true;
						break;
					}
				}
			}

			// Second pass: fallback random placement (original logic)
			if (!placed) {
				for (let attempt = 0; attempt < 30; attempt++) {
					const angle = rng() * Math.PI * 2;
					const dist = 15 + Math.floor(rng() * 25);
					px = ((city.x + Math.round(Math.cos(angle) * dist)) % mapWidth + mapWidth) % mapWidth;
					py = ((city.y + Math.round(Math.sin(angle) * dist)) % mapHeight + mapHeight) % mapHeight;
					if (isLand(px, py) && !isTooClose_(px, py, settlements, villages, mapWidth, mapHeight)) {
						placed = true;
						break;
					}
				}
			}

			if (!placed) {
				continue;
			}

			const height = getHeight(px, py);
			const localResources = resourcesForTerrain(height);
			const population = 10 + Math.floor(rng() * 90); // 10-100

			const villageSeed = seed + idCounter * 777;
			const flagGen = new FlagGenerator(villageSeed);

			villages.push({
				id: idCounter++,
				name: generateVillageName(rng),
				x: px,
				y: py,
				population,
				mood: (['Prosperous', 'Stable', 'Tense'] as const)[Math.floor(rng() * 3)],
				banner: flagGen.generate().toDataURL(),
				inventory: createInventory(),
				history: {days: [], population: []},
				eco: createEconomyState(localResources),
				nearestCityId: city.id,
				lastTradeDay: 0,
			});
		}
	}

	return villages;
}

/** Check if (x,y) is too close to any city (<10 tiles) or village (<8 tiles). */
function isTooClose_(
	x: number, y: number,
	settlements: Settlement[], villages: Village[],
	_mapWidth: number, _mapHeight: number,
): boolean {
	for (const s of settlements) {
		const dx = x - s.x;
		const dy = y - s.y;
		if (dx * dx + dy * dy < 100) {
			return true;
		}
	}

	for (const v of villages) {
		const dx = x - v.x;
		const dy = y - v.y;
		if (dx * dx + dy * dy < 64) {
			return true;
		}
	}

	return false;
}

/** Find nearest road cell within `radius` of (cx,cy). Returns undefined if none. */
function snapToRoad_(
	cx: number, cy: number,
	roadMask: Uint8Array,
	mapWidth: number, mapHeight: number,
	radius: number,
): {x: number; y: number} | undefined {
	let bestDist = radius * radius + 1;
	let bestX = cx;
	let bestY = cy;

	for (let dy = -radius; dy <= radius; dy++) {
		for (let dx = -radius; dx <= radius; dx++) {
			const d2 = dx * dx + dy * dy;
			if (d2 >= bestDist) {
				continue;
			}

			const nx = ((cx + dx) % mapWidth + mapWidth) % mapWidth;
			const ny = ((cy + dy) % mapHeight + mapHeight) % mapHeight;
			if (roadMask[ny * mapWidth + nx] > 0) {
				bestDist = d2;
				bestX = nx;
				bestY = ny;
			}
		}
	}

	return bestDist <= radius * radius ? {x: bestX, y: bestY} : undefined;
}

function createStarterInventory(): Inventory {
	const inv = createInventory(); // Universal 64 slots
	addItem(inv, makeItem('potion_hp', 2));
	addItem(inv, makeItem('food_bread', 5));
	return inv;
}

function starterArmy(): ArmyComposition {
	const a = defaultArmy();
	a[UnitType.Swordsman] = 3;
	a[UnitType.Archer] = 2;
	a[UnitType.Spearman] = 1;
	return a;
}

function createStarterSpellBook(): SpellBook {
	const book = createSpellBook();
	learnSpell(book, 'magic_bolt');
	return book;
}

function createFactions(): Record<string, Faction> {
	return {
		empire: {
			id: 'empire', name: 'Empire of Light', color: '#fbbf24',
			description: 'Theocratic empire. Magic is forbidden.',
			relations: {
				magika: -80, cults: -100, timaert: 20, wildlife: -50, monsters: -80,
			},
		},
		magika: {
			id: 'magika', name: 'Magocracy', color: '#a78bfa',
			description: 'Ruled by powerful mages. High magic economy.',
			relations: {
				empire: -80, barbarians: -40, timaert: 10, wildlife: -50, monsters: -80,
			},
		},
		barbarians: {
			id: 'barbarians', name: 'Barbarian Kings', color: '#ef4444',
			description: 'Feudal lords ruling by might and steel.',
			relations: {
				empire: -20, magika: -40, cults: 10, wildlife: -50, monsters: -80,
			},
		},
		timaert: {
			id: 'timaert', name: 'Republic of Timaert', color: '#3b82f6',
			description: 'Maritime trade republic. Neutral and wealthy.',
			relations: {
				empire: 20, magika: 10, barbarians: 0, wildlife: -50, monsters: -80,
			},
		},
		cults: {
			id: 'cults', name: 'Black Cults', color: '#581c87',
			description: 'Worshippers of void and dead gods.',
			relations: {
				empire: -100, magika: -50, barbarians: 10, wildlife: -30, monsters: 20,
			},
		},
		wildlife: {
			id: 'wildlife', name: 'Wildlife', color: '#6b8e23',
			description: 'Wild animals — predators attack, prey flees.',
			relations: {
				empire: -50, magika: -50, barbarians: -50, timaert: -50, cults: -30, monsters: -30,
			},
		},
		monsters: {
			id: 'monsters', name: 'Monsters', color: '#8b0000',
			description: 'Hostile creatures lurking in the wilds.',
			relations: {
				empire: -80, magika: -80, barbarians: -80, timaert: -80, cults: 20, wildlife: -30,
			},
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
			eco: createEconomyState(),
		};
	});

	// Spawn player at a random settlement
	const spawnIdx = Math.floor(rng() * settlements.length);
	const spawn = settlements[spawnIdx];

	const attrs = defaultAttributes();
	const skills = defaultSkills();
	const combat = calculateCombatStats(attrs, skills);

	return {
		version: kSaveVersion,
		saveName: 'Autosave',
		savedAt: new Date().toISOString(),
		mapParams: mapParameters,
		settlements,
		villages: [],
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
			spellBook: createStarterSpellBook(),
		},
		worldTime: {day: 1, hour: 8, minute: 0},
		subState: {type: 'exploring'},
		seed: mapParameters.seed,
		deserterPool: defaultArmy(),
		activeTradeRoutes: [],
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
		version: kSaveVersion,
		saveName: 'New Game',
		savedAt: new Date().toISOString(),
		mapParams: parameters,
		settlements: [],
		villages: [],
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
			spellBook: createStarterSpellBook(),
		},
		worldTime: {day: 1, hour: 8, minute: 0},
		subState: {type: 'exploring'},
		seed,
		deserterPool: defaultArmy(),
		activeTradeRoutes: [],
	};
}

export {defaultParameters as defaultParams, type LayerParameters as LayerParams} from '../webgl/webgl-context';

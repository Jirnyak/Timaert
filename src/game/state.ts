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
	type ArmyComposition, defaultArmy,
} from './army';
import {type SpellBook, createSpellBook, learnSpell} from './spells';
import {xorshift32} from './rng';
import {
	type EconomyState, type TradeRoute,
	createEconomyState, resourcesForTerrain,
} from './economy';
import type {Quest} from './quests/quest-types';
import type {Marker} from './markers';
import {torusDistSq} from './torus';
import {
	KINGDOM_DEFS, type KingdomDef, type KingdomLineage, type Politik, type Kingdom,
} from './politik';
import {type Language, generateName as generateLangName} from './language';

// === Factions ===
// 1 kingdom = 1 faction. Faction ids match kingdom ids (see politik.ts).
// Plus three universal non-territorial factions (cults, wildlife, monsters).
export type FactionId = string;

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
	/** Owning kingdom (index into KINGDOM_DEFS, -1 = unowned). */
	kingdomIdx: number;
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
	/** Owning kingdom (index into KINGDOM_DEFS, -1 = unowned). */
	kingdomIdx: number;
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
	activeQuests: Quest[];
	completedQuestIds: string[];
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
export const kSaveVersion = 8;

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
	/** Map markers (quests, POIs, waypoints). */
	markers: Marker[];
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

// === Settlement names ===
//
// Settlement / village names are generated procedurally from each kingdom's
// Language (see `politik.ts` and `language.ts`). There are no hardcoded
// prefix/suffix tables — every kingdom has its own phonotactics and every
// city/village name comes from there.

function kingdomLanguageByDefIdx(politik: Politik, defIdx: number): Language | undefined {
	for (const k of Object.values(politik.kingdoms)) {
		if (k.defIdx === defIdx) {
			return k.language;
		}
	}

	return undefined;
}

function nameFromLanguage(
	lang: Language | undefined,
	rng: () => number,
	used: Set<string>,
	fallback: string,
): string {
	if (!lang) {
		return `${fallback} ${used.size + 1}`;
	}

	for (let attempt = 0; attempt < 30; attempt++) {
		const n = generateLangName(lang, rng);
		if (!used.has(n)) {
			used.add(n);
			return n;
		}
	}

	let i = 2;
	const base = generateLangName(lang, rng);
	while (used.has(`${base} ${i}`)) {
		i++;
	}

	const final = `${base} ${i}`;
	used.add(final);
	return final;
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
	politik: Politik,
	roadMask?: Uint8Array,
	targetTotal = 100,
): Village[] {
	const rng = xorshift32(seed + 3333);
	const villages: Village[] = [];
	let idCounter = settlements.length;
	const usedNames = new Set<string>(settlements.map(s => s.name));

	if (settlements.length === 0) {
		return villages;
	}

	// Distribute targetTotal villages across cities, weighted by population
	// so capitals + larger cities act as the natural anchors. Ensures every
	// city gets at least one village if there's budget for it.
	const totalPop = settlements.reduce((sum, s) => sum + s.population, 0);
	const villageCounts = settlements.map(s => {
		const share = (s.population / Math.max(1, totalPop)) * targetTotal;
		// Smooth random rounding so the totals fluctuate naturally
		const base = Math.floor(share);
		const frac = share - base;
		return base + (rng() < frac ? 1 : 0);
	});

	for (const [cityIdx, city] of settlements.entries()) {
		const count = villageCounts[cityIdx];
		for (let v = 0; v < count; v++) {
			const placement = tryPlaceVillage_(rng, city, settlements, villages, mapWidth, mapHeight, isLand, roadMask);
			if (!placement) {
				continue;
			}

			const {x: px, y: py} = placement;

			const height = getHeight(px, py);
			const localResources = resourcesForTerrain(height);
			const population = 10 + Math.floor(rng() * 90); // 10-100

			const villageSeed = seed + idCounter * 777;
			const flagGen = new FlagGenerator(villageSeed);
			const lang = kingdomLanguageByDefIdx(politik, city.kingdomIdx);

			villages.push({
				id: idCounter++,
				name: nameFromLanguage(lang, rng, usedNames, 'Village'),
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
				kingdomIdx: city.kingdomIdx,
			});
		}
	}

	return villages;
}

function tryPlaceVillage_(
	rng: () => number,
	city: Settlement,
	settlements: Settlement[],
	villages: Village[],
	mapWidth: number,
	mapHeight: number,
	isLand: (x: number, y: number) => boolean,
	roadMask: Uint8Array | undefined,
): {x: number; y: number} | undefined {
	if (roadMask) {
		for (let attempt = 0; attempt < 20; attempt++) {
			const angle = rng() * Math.PI * 2;
			const dist = 15 + Math.floor(rng() * 25);
			const cx = ((city.x + Math.round(Math.cos(angle) * dist)) % mapWidth + mapWidth) % mapWidth;
			const cy = ((city.y + Math.round(Math.sin(angle) * dist)) % mapHeight + mapHeight) % mapHeight;
			if (!isLand(cx, cy) || isTooClose_(cx, cy, settlements, villages, mapWidth, mapHeight)) {
				continue;
			}

			const snapped = snapToRoad_(cx, cy, roadMask, mapWidth, mapHeight, 3);
			if (!snapped) {
				continue;
			}

			if (!isLand(snapped.x, snapped.y)) {
				continue;
			}

			if (isTooClose_(snapped.x, snapped.y, settlements, villages, mapWidth, mapHeight)) {
				continue;
			}

			return {x: snapped.x, y: snapped.y};
		}
	}

	for (let attempt = 0; attempt < 30; attempt++) {
		const angle = rng() * Math.PI * 2;
		const dist = 15 + Math.floor(rng() * 25);
		const px = ((city.x + Math.round(Math.cos(angle) * dist)) % mapWidth + mapWidth) % mapWidth;
		const py = ((city.y + Math.round(Math.sin(angle) * dist)) % mapHeight + mapHeight) % mapHeight;
		if (isLand(px, py) && !isTooClose_(px, py, settlements, villages, mapWidth, mapHeight)) {
			return {x: px, y: py};
		}
	}

	return undefined;
}

function isTooClose_(
	x: number, y: number,
	settlements: Settlement[], villages: Village[],
	mapWidth: number, mapHeight: number,
): boolean {
	for (const s of settlements) {
		if (torusDistSq(x, y, s.x, s.y, mapWidth, mapHeight) < 100) {
			return true;
		}
	}

	for (const v of villages) {
		if (torusDistSq(x, y, v.x, v.y, mapWidth, mapHeight) < 64) {
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
	return defaultArmy();
}

function createStarterSpellBook(): SpellBook {
	const book = createSpellBook();
	learnSpell(book, 'magic_bolt');
	return book;
}

// Universal (non-territorial) factions present in every world.
type UniversalFactionDef = {
	id: string;
	name: string;
	description: string;
	color: string;
};

const UNIVERSAL_FACTIONS: UniversalFactionDef[] = [
	{
		id: 'cults', name: 'Black Cults', color: '#581c87',
		description: 'Worshippers of void and dead gods.',
	},
	{
		id: 'wildlife', name: 'Wildlife', color: '#6b8e23',
		description: 'Wild animals — predators attack, prey flees.',
	},
	{
		id: 'monsters', name: 'Monsters', color: '#8b0000',
		description: 'Hostile creatures lurking in the wilds.',
	},
];

// === Relation policies ===
// Each unordered pair of factions resolves to ONE policy band. The seeded
// RNG samples a value within the band; the same value is stored on both
// sides (relations are symmetric at world creation).
type RelationBand = readonly [number, number];

const ALLY: RelationBand = [55, 90];
const WAR: RelationBand = [-100, -75];
const HOSTILE_LIGHT: RelationBand = [-50, 0];
const NEUTRAL_BAND: RelationBand = [-50, 50];
const ANY_BAND: RelationBand = [-100, 100];

// Specific kingdom-pair overrides (by kingdom id).
const PAIR_OVERRIDES: Array<[string, string, RelationBand]> = [
	['timaert', 'northern_magica', ALLY],
	['empire', 'lower_magica', ALLY],
	['timaert', 'cults', WAR],
];

function pairKey(a: string, b: string): string {
	return a < b ? `${a}|${b}` : `${b}|${a}`;
}

function isMagica(def: KingdomDef | undefined): boolean {
	return def?.lineage === 'magika';
}

function isBarbarian(def: KingdomDef | undefined): boolean {
	return def?.lineage === 'barbarians';
}

function resolveBand(a: string, b: string, defs: Map<string, KingdomDef>): RelationBand {
	for (const [x, y, band] of PAIR_OVERRIDES) {
		if (pairKey(x, y) === pairKey(a, b)) {
			return band;
		}
	}

	const da = defs.get(a);
	const db = defs.get(b);

	// Magicas vs Cults — always war.
	if ((isMagica(da) && b === 'cults') || (isMagica(db) && a === 'cults')) {
		return WAR;
	}

	// Magicas vs Barbarians — always war.
	if ((isMagica(da) && isBarbarian(db)) || (isMagica(db) && isBarbarian(da))) {
		return WAR;
	}

	// Empire vs other Magicas — hostile light (covered overrides for lower_magica).
	if ((da?.lineage === 'empire' && isMagica(db)) || (db?.lineage === 'empire' && isMagica(da))) {
		return HOSTILE_LIGHT;
	}

	// Magicas vs Magicas — fully random (could be war).
	if (isMagica(da) && isMagica(db)) {
		return ANY_BAND;
	}

	// Barbarians vs anything else — fully random.
	if (isBarbarian(da) || isBarbarian(db)) {
		return ANY_BAND;
	}

	// Timaert vs anything else — narrow neutral.
	if (da?.id === 'timaert' || db?.id === 'timaert') {
		return NEUTRAL_BAND;
	}

	// Empire vs anything else (non-magica) — narrow neutral.
	if (da?.lineage === 'empire' || db?.lineage === 'empire') {
		return NEUTRAL_BAND;
	}

	// Lake Duchy + universals (cults/wildlife/monsters) toward each other —
	// keep the universals mildly hostile, otherwise neutral.
	if (a === 'cults' || b === 'cults') {
		return [-60, -20];
	}

	if (a === 'monsters' || b === 'monsters') {
		return [-80, -20];
	}

	if (a === 'wildlife' || b === 'wildlife') {
		return [-30, 30];
	}

	return NEUTRAL_BAND;
}

function sampleBand(rng: () => number, band: RelationBand): number {
	const [lo, hi] = band;
	return Math.round(lo + rng() * (hi - lo));
}

function createFactions(seed: number): Record<string, Faction> {
	const rng = xorshift32(seed + 7777);
	const factions: Record<string, Faction> = {};
	const defs = new Map<string, KingdomDef>(KINGDOM_DEFS.map(d => [d.id, d]));

	const allIds: string[] = [
		...KINGDOM_DEFS.map(d => d.id),
		...UNIVERSAL_FACTIONS.map(u => u.id),
	];

	// Build symmetric relations matrix.
	const matrix = new Map<string, number>();
	for (let i = 0; i < allIds.length; i++) {
		for (let j = i + 1; j < allIds.length; j++) {
			const a = allIds[i];
			const b = allIds[j];
			const band = resolveBand(a, b, defs);
			matrix.set(pairKey(a, b), sampleBand(rng, band));
		}
	}

	const relationsFor = (id: string): Record<string, number> => {
		const out: Record<string, number> = {};
		for (const other of allIds) {
			if (other === id) {
				continue;
			}

			out[other] = matrix.get(pairKey(id, other))!;
		}

		return out;
	};

	for (const def of KINGDOM_DEFS) {
		factions[def.id] = {
			id: def.id,
			name: def.name,
			color: def.color,
			description: lineageDescription(def.lineage),
			relations: relationsFor(def.id),
		};
	}

	for (const u of UNIVERSAL_FACTIONS) {
		factions[u.id] = {
			id: u.id,
			name: u.name,
			color: u.color,
			description: u.description,
			relations: relationsFor(u.id),
		};
	}

	return factions;
}

function lineageDescription(lineage: KingdomLineage): string {
	switch (lineage) {
		case 'empire': {
			return 'Theocratic empire. Magic is forbidden.';
		}

		case 'magika': {
			return 'Ruled by powerful mages. High magic economy.';
		}

		case 'barbarians': {
			return 'Feudal lords ruling by might and steel.';
		}

		case 'timaert': {
			return 'Maritime trade republic. Neutral and wealthy.';
		}
	}
}

function createInitialReputation(): Reputation {
	const rep: Reputation = {};
	for (const def of KINGDOM_DEFS) {
		rep[def.id] = 0;
	}

	for (const u of UNIVERSAL_FACTIONS) {
		rep[u.id] = u.id === 'cults' ? -10 : 0;
	}

	return rep;
}

// === Create initial game state from map params and cities ===
export function createGameState(
	mapParameters: LayerParameters,
	cities: City[],
	mapWidth: number,
	mapHeight: number,
	politik: Politik,
): GameState {
	const rng = xorshift32(mapParameters.seed + 999);
	const usedNames = new Set<string>();

	const settlements: Settlement[] = cities.map((city, i) => {
		const settlementSeed = mapParameters.seed + i * 555;
		const flagGen = new FlagGenerator(settlementSeed);
		const banner = flagGen.generate().toDataURL();

		// Capitals are roughly twice as populous as ordinary cities.
		const basePop = Math.floor(rng() * 900) + 100;
		const population = city.isCapital ? basePop * 2 : basePop;
		const economy = ['farming', 'mining', 'trade', 'fishing', 'crafting'][Math.floor(rng() * 5)];
		const mood: SettlementMood = (['Prosperous', 'Stable', 'Tense', 'Unrest', 'Revolt'] as const)[Math.floor(rng() * 5)];

		// Create seeded RNG for this settlement's inventory
		const settlementRng = xorshift32(settlementSeed + 1000);
		const inventory = generateSettlementInventory(population, economy, settlementRng);

		const lang = kingdomLanguageByDefIdx(politik, city.kingdomIdx);

		return {
			id: i,
			name: nameFromLanguage(lang, rng, usedNames, 'City'),
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
			kingdomIdx: city.kingdomIdx,
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
		factions: createFactions(mapParameters.seed),
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
			reputation: createInitialReputation(),
			army: starterArmy(),
			characterData: CharacterManager.generateRandomCharacter(paletteManager.getDefaultPaletteState()),
			codexUnlocked: ['cosmology', 'attributes', 'perks_skills', 'market', 'settlements'],
			eventLog: [],
			spellBook: createStarterSpellBook(),
			activeQuests: [],
			completedQuestIds: [],
		},
		worldTime: {day: 1, hour: 8, minute: 0},
		subState: {type: 'exploring'},
		seed: mapParameters.seed,
		deserterPool: defaultArmy(),
		activeTradeRoutes: [],
		markers: [],
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
		factions: createFactions(parameters.seed),
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
			reputation: createInitialReputation(),
			army: starterArmy(),
			characterData: CharacterManager.generateRandomCharacter(paletteManager.getDefaultPaletteState()),
			codexUnlocked: ['cosmology', 'attributes', 'perks_skills', 'market', 'settlements'],
			eventLog: [],
			spellBook: createStarterSpellBook(),
			activeQuests: [],
			completedQuestIds: [],
		},
		worldTime: {day: 1, hour: 8, minute: 0},
		subState: {type: 'exploring'},
		seed,
		deserterPool: defaultArmy(),
		activeTradeRoutes: [],
		markers: [],
	};
}

export {defaultParameters as defaultParams, type LayerParameters as LayerParams} from '../webgl/webgl-context';

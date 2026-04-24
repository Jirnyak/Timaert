<script lang="ts">
	import {onMount} from 'svelte';
	import type {PlayerState, GameState, AnySettlement} from '../game/state';
	import {
		SubworldEngine, SubworldRenderer, SubworldRenderer3D,
		findWalkable, makeEntity,
		createCitizenSpriteSheet, renderPlayerSprite,
		spawnArmy, spawnCityNpcs, populateCell,
		type SubworldConfig, type SubworldEntity, type TraversabilityGrid,
		type ZoneAction, type SubworldResult, type FightContext,
		type BillboardEntity,
		createCamera, updateCameraHeight, rotateCamera, moveVector, moveVector3d,
		type CameraState,
		SeamlessSubworldManager, CELL_SIZE,
		type CellResolver, type ModeResolver, type LoadedCell,
	} from '../game/subworld';
	import {xorshift32} from '../game/rng';
	import {
		type SubworldMode, type MapData,
		TILE_ROAD, TILE_SQUARE,
		findTileNear, collectRoadNearHouses,
		CellFeature, Biome, structureFloorAt,
	} from '../game/subworld/map-data';
	import {SUBWORLD_SP_PER_1000} from '../game/movement-cost';
	import {HEIGHT_SCALE} from '../game/subworld/camera';
	import {biomeFromClimate} from '../game/biomes';
	import {type NPC, NPCType, settlementFaction} from '../game/npc';
	import {
		calculateDerived, tryLevelUp, getCarryCapacity, getOverloadPenalty,
	} from '../game/attributes';
	import {addItem, getInventoryWeight} from '../game/items';
	import {loadTrack, playTrack} from '../game/audio';
	import {
		getSpell, canCast, startCast, tickSpellBook, spellDamage, spellRadius,
		SPELL_LIST, learnSpell,
	} from '../game/spells';
	import {
		color, btnProps, messageStyle, mutedStyle, fmtStat,
	} from '../ui/theme';
	import {FeatureType, getFeatureAt, type FeatureLayer} from '../game/features';
	import {type ZoneLayer, getZoneAt} from '../game/zones';
	import DebugOverlay from './DebugOverlay.svelte';
	import DeathOverlay from './DeathOverlay.svelte';
	import SharedOverlays from './SharedOverlays.svelte';
	import {sharedOverlayForKey, toggleSharedOverlay, type SharedOverlayId} from './shared-overlays';

	type Props = {
		player: PlayerState;
		gameState: GameState;
		settlement?: AnySettlement;
		seed: number;
		mode: SubworldMode;
		fightContext?: FightContext;
		onExit: (result?: SubworldResult) => void;
		onTrade: () => void;
		onNewGame: () => void;
		onBackToTitle: () => void;
		onReborn: () => void;
		/** Macroworld feature layer for seamless mode. */
		featureLayer?: FeatureLayer;
		/** Macroworld difficulty zone layer (drives monster scaling, encounters). */
		zoneLayer?: ZoneLayer;
		/** Macroworld traversability data (height per cell, 0-255) for seamless. */
		macroHeightData?: Uint8Array;
		/** Macroworld moisture per cell (0-255). */
		macroMoistureData?: Uint8Array;
		/** Macroworld temperature per cell (0-255). */
		macroTemperatureData?: Uint8Array;
		/** Macroworld dimensions. */
		mapW?: number;
		mapH?: number;
		/** Macroworld sea level (0..1 height threshold). */
		seaLevel?: number;
		/** Callback to update macroworld player position when seamless cell changes. */
		onCellChange?: (cx: number, cy: number) => void;
		/** Macroworld NPCs — used to populate subworld cells with NPC squads. */
		macroNpcs?: NPC[];
	};

	let {
		player = $bindable(), gameState, settlement, seed, mode,
		fightContext, onExit, onTrade,
		onNewGame, onBackToTitle, onReborn,
		featureLayer, macroHeightData,
		macroMoistureData, macroTemperatureData,
		zoneLayer,
		mapW = 512, mapH = 512,
		seaLevel = 0.4,
		onCellChange,
		macroNpcs = [],
	}: Props = $props();

	let canvas: HTMLCanvasElement;
	let canvas3d: HTMLCanvasElement;
	let largeMapCanvas: HTMLCanvasElement;
	let largeMapRenderer: SubworldRenderer | undefined;
	let message = $state('');
	let messageTimer = 0;
	let lowSpMsgCooldown = 0;
	let hitFlashTimer = $state(0);
	let lastPlayerHp = Number.POSITIVE_INFINITY;
	let engine: SubworldEngine | undefined;
	let renderer: SubworldRenderer | undefined;
	let renderer3d: SubworldRenderer3D | undefined;
	let camera: CameraState | undefined;
	let currentMapData: MapData | undefined;
	let animFrame = 0;
	let loading = $state(true);
	let showLargeMap = $state(false);
	let friendlyCount = $state(0);
	let enemyCount = $state(0);
	let dangerLevel = $state<'green' | 'yellow' | 'red'>('green');
	/** Latest visible combat log entries (rolling, age-filtered). */
	let combatLogView = $state<Array<{text: string; age: number}>>([]);
	const COMBAT_LOG_VISIBLE_SECONDS = 4;
	const COMBAT_LOG_MAX_LINES = 5;
	let exitBlockedFlash = $state(0);
	const gemColor = $derived(dangerLevel === 'green' ? '#3fbf4a' : (dangerLevel === 'yellow' ? '#e8c84a' : '#e0322a'));
	const gemTitle = $derived(dangerLevel === 'green'
		? 'Safe — no enemies nearby. You may leave.'
		: (dangerLevel === 'yellow'
			? 'Caution — enemies nearby. Cannot leave.'
			: 'Danger — enemies in melee range!'));
	let paused = $state(false);
	// Shared overlays openable in both macroworld and subworld.
	let sharedOverlay = $state<SharedOverlayId | undefined>(undefined);
	let showDebug = $state(false);
	let showDeath = $state(false);
	let debugFps = $state(0);
	let debugFrameDt = $state(0);

	// Spell casting state
	const mouseWorldX = 0;
	const mouseWorldY = 0;

	/** Seamless manager — used for nature/wilds mode. */
	let seamless: SeamlessSubworldManager | undefined;
	const spawnedCells = new Set<string>();

	/** Accumulated subworld distance for SP drain (resets every 1000 units). */
	let distanceAccum = 0;

	const activeSpell = $derived(getSpell(player.spellBook.activeSpellId));

	const SEAMLESS_SIZE = CELL_SIZE * 3; // 3072 — full 3×3 grid
	const CITY_SCALE = 40;
	const pressed = new Set<string>();

	/** Dynamic location name — updates when center cell changes during shifts. */
	let centerName = $state(settlement ? settlement.name : 'The Wilds');
	const locationName = $derived(centerName);

	// ── Helpers ──────────────────────────────────────────────────

	/** Parse hex color string to [r, g, b] in 0–1 range. */
	function parseHexColor(hex: string): [number, number, number] {
		const match = /^#?([\da-f]{2})([\da-f]{2})([\da-f]{2})/i.exec(hex);
		if (!match) {
			return [0.5, 0.5, 0.5];
		}

		return [
			Number.parseInt(match[1], 16) / 255,
			Number.parseInt(match[2], 16) / 255,
			Number.parseInt(match[3], 16) / 255,
		];
	}

	/** Compute player melee damage from RPG attributes. */
	function playerDamage(): number {
		const derived = calculateDerived(player.attributes, player.skills);
		const base = 10;
		return Math.floor(base + derived.rawPhysDamage);
	}

	/** Build faction context for the engine from game state. */
	function factionContext(): {
		factions: Record<string, {relations: Record<string, number>}>;
		reputation: Record<string, number>;
	} {
		const factions: Record<string, {relations: Record<string, number>}> = {};
		for (const [id, f] of Object.entries(gameState.factions)) {
			factions[id] = {relations: {...f.relations}};
		}

		return {factions, reputation: {...player.reputation}};
	}

	/** Look up a settlement or village at macro coords (cx, cy). */
	function findSettlementAt(cx: number, cy: number): AnySettlement | undefined {
		for (const s of gameState.settlements) {
			if (s.x === cx && s.y === cy) {
				return s;
			}
		}

		for (const v of gameState.villages) {
			if (v.x === cx && v.y === cy) {
				return v;
			}
		}

		return undefined;
	}

	/** Find macroworld NPCs whose position matches cell (cx, cy). */
	function findMacroNpcsAt(cx: number, cy: number): NPC[] {
		if (!macroNpcs) {
			return [];
		}

		const wrappedX = ((cx % mapW) + mapW) % mapW;
		const wrappedY = ((cy % mapH) + mapH) % mapH;
		return macroNpcs.filter(n =>
			Math.floor(n.x) === wrappedX && Math.floor(n.y) === wrappedY);
	}

	/** Resolve cell biome/feature for a macroworld cell. */
	function resolveCellMeta(cx: number, cy: number): {
		biome: Biome; feature: CellFeature; landmark: string | undefined; landmarkParam: number; zoneLevel: number;
	} {
		const wrappedX = ((cx % mapW) + mapW) % mapW;
		const wrappedY = ((cy % mapH) + mapH) % mapH;

		let feature = CellFeature.None;
		if (featureLayer) {
			feature = getFeatureAt(featureLayer, wrappedX, wrappedY) as unknown as CellFeature;
		}

		let landmark: string | undefined;
		let landmarkParam = 0;
		const found = findSettlementAt(wrappedX, wrappedY);
		if (found) {
			landmark = 'garrison' in found ? 'city' : 'village';
			landmarkParam = found.population;
		} else {
			const spire = gameState.spires.find(s => s.x === wrappedX && s.y === wrappedY);
			if (spire) {
				landmark = 'spire';
				landmarkParam = spire.id;
			}
		}

		const macroIdx = wrappedY * mapW + wrappedX;
		const macroH = macroHeightData ? macroHeightData[macroIdx] / 255 : 0.5;
		const temp01 = macroTemperatureData ? macroTemperatureData[macroIdx] / 255 : 0.5;
		const moist01 = macroMoistureData ? macroMoistureData[macroIdx] / 255 : 0.5;
		const biome = macroH < seaLevel ? Biome.Water : biomeFromClimate(temp01, moist01);
		const zoneLevel = zoneLayer ? getZoneAt(zoneLayer, wrappedX, wrappedY) : 0;

		return {
			biome, feature, landmark, landmarkParam, zoneLevel,
		};
	}

	// ── Cull entities outside the 3×3 grid (in-place to preserve array ref) ──

	function cullOutOfBounds(eng: SubworldEngine): void {
		for (let i = eng.entities.length - 1; i >= 0; i--) {
			const ent = eng.entities[i];
			if (ent !== eng.player
				&& (ent.x < 0 || ent.x >= SEAMLESS_SIZE
					|| ent.y < 0 || ent.y >= SEAMLESS_SIZE)) {
				eng.entities.splice(i, 1);
			}
		}
	}

	// ── Spawn NPCs for newly loaded cells (seamless transitions) ──

	async function spawnForLoadedCells(
		eng: SubworldEngine,
		sm: SeamlessSubworldManager,
		loaded: LoadedCell[],
	): Promise<void> {
		const compTileGrid = sm.compositeTileGrid();
		const compTraversability = sm.compositeTraversability();
		const traversability: TraversabilityGrid = {
			width: compTraversability.width, height: compTraversability.height,
			data: compTraversability.data,
		};

		for (const cell of loaded) {
			const cellRng = xorshift32(cell.seed + 7);
			const {gx: cellGx, gy: cellGy} = sm.cellToGlobal(cell.cx, cell.cy, CELL_SIZE / 2, CELL_SIZE / 2);
			const nextId = {value: 0};
			const meta = resolveCellMeta(cell.cx, cell.cy);

			// Ensure citizenSheet for cities/villages
			if ((cell.mode === 'city' || cell.mode === 'village') && !eng.config.citizenSheet) {
				const found = findSettlementAt(cell.cx, cell.cy);
				if (found) {
					// eslint-disable-next-line no-await-in-loop
					const sheet = await createCitizenSpriteSheet(found.population);
					(eng.config as any).citizenSheet = sheet;
					if (renderer3d) {
						renderer3d.uploadSpriteSheet(sheet.canvas);
					}
				}
			}

			// Build city spot finder for settlements
			let findCitySpot: (() => {x: number; y: number} | undefined) | undefined;
			if (cell.mode === 'city' || cell.mode === 'village') {
				const cellOrigin = sm.cellToGlobal(cell.cx, cell.cy, 0, 0);
				const allRoads = collectRoadNearHouses(compTileGrid.data, compTileGrid.width, compTileGrid.height);
				const cellPool = allRoads.filter(p =>
					p.x >= cellOrigin.gx && p.x < cellOrigin.gx + CELL_SIZE
					&& p.y >= cellOrigin.gy && p.y < cellOrigin.gy + CELL_SIZE);
				findCitySpot = () => cellPool.length === 0
					? undefined
					: cellPool[Math.floor(cellRng() * cellPool.length)];
			}

			// Universal cell population
			const spawned = populateCell({
				cellX: cell.cx, cellY: cell.cy,
				biome: meta.biome, feature: meta.feature,
				landmark: meta.landmark, landmarkParam: meta.landmarkParam,
				globalCx: cellGx, globalCy: cellGy,
				seed: cell.seed, traversability,
				macroNpcs: findMacroNpcsAt(cell.cx, cell.cy),
				cityFaction: settlementFaction(cell.cx, cell.cy),
				findCitySpot,
				citizenSheetCount: eng.config.citizenSheet?.count,
				zoneLevel: meta.zoneLevel,
			}, nextId, cellRng);

			for (const ent of spawned) {
				const {id: _, ...rest} = ent;
				eng.addEntity(rest as any);
			}

			// Trade zone + Inn for settlements
			if (cell.mode === 'city' || cell.mode === 'village') {
				const tradeSpot = findTileNear(compTileGrid.data, compTileGrid.width, compTileGrid.height, cellGx, cellGy, TILE_SQUARE, 30);
				if (tradeSpot) {
					eng.addEntity({
						kind: 'zone', x: tradeSpot.x + 3, y: tradeSpot.y + 3, radius: 8,
						label: 'Market', color: 'rgba(255,255,100,0.2)', action: {type: 'trade'},
					} as any);
				}

				const found = findSettlementAt(cell.cx, cell.cy);
				const innSpot = findTileNear(compTileGrid.data, compTileGrid.width, compTileGrid.height, cellGx + 20, cellGy - 15, TILE_ROAD, 40);
				if (innSpot && found) {
					const cost = 'mood' in found && found.mood === 'Prosperous' ? 5 : 10;
					eng.addEntity({
						kind: 'zone', x: innSpot.x, y: innSpot.y, radius: 8,
						label: 'Inn', color: 'rgba(100,200,255,0.2)', action: {type: 'rest', cost},
					} as any);
				}
			}
		}
	}

	// ── Config builders ─────────────────────────────────────────

	async function buildConfig(): Promise<SubworldConfig> {
		const playerSheet = await renderPlayerSprite(player.characterData);

		// ── Resolve macroworld cell context ─────────────────
		const resolveCell: CellResolver = (cx, cy) => {
			const wrappedX = ((cx % mapW) + mapW) % mapW;
			const wrappedY = ((cy % mapH) + mapH) % mapH;
			let feature = CellFeature.None;
			if (featureLayer) {
				const ft = getFeatureAt(featureLayer, wrappedX, wrappedY);
				feature = ft as unknown as CellFeature;
			}

			let landmark: 'city' | 'village' | 'ruin' | 'spire' | undefined;
			let landmarkParam = 0;
			for (const s of gameState.settlements) {
				if (s.x === wrappedX && s.y === wrappedY) {
					landmark = 'city';
					landmarkParam = s.population;
					break;
				}
			}

			if (!landmark) {
				for (const v of gameState.villages) {
					if (v.x === wrappedX && v.y === wrappedY) {
						landmark = 'village';
						landmarkParam = v.population;
						break;
					}
				}
			}

			if (!landmark) {
				for (const sp of gameState.spires) {
					if (sp.x === wrappedX && sp.y === wrappedY) {
						landmark = 'spire';
						landmarkParam = sp.id;
						break;
					}
				}
			}

			const macroHeight = macroHeightData
				? macroHeightData[wrappedY * mapW + wrappedX] / 255
				: 0.5;

			const macroIdx = wrappedY * mapW + wrappedX;
			const temp01 = macroTemperatureData
				? macroTemperatureData[macroIdx] / 255
				: 0.5;
			const moist01 = macroMoistureData
				? macroMoistureData[macroIdx] / 255
				: 0.5;
			const biome = macroHeight < seaLevel
				? Biome.Water
				: biomeFromClimate(temp01, moist01);

			return {
				cellX: wrappedX,
				cellY: wrappedY,
				feature,
				biome,
				landmark,
				landmarkParam,
				macroHeight,
				temperature: temp01,
				zoneLevel: zoneLayer ? getZoneAt(zoneLayer, wrappedX, wrappedY) : 0,
				seed: gameState.seed + wrappedX * 1000 + wrappedY,
			};
		};

		const resolveMode: ModeResolver = ctx => {
			if (ctx.landmark) {
				return ctx.landmark;
			}

			// Roads through rivers: if a water cell has a road feature,
			// use the road generator so the road crosses naturally.
			if (ctx.feature === CellFeature.Road || ctx.feature === CellFeature.DirtRoad) {
				return 'road';
			}

			if (ctx.biome === Biome.Water) {
				return 'water';
			}

			if (ctx.biome === Biome.Swamp) {
				return 'swamp';
			}

			if (ctx.feature === CellFeature.Tree) {
				return 'forest';
			}

			if (ctx.feature === CellFeature.Mountain) {
				return 'mountain';
			}

			return 'grassland';
		};

		// Create seamless manager and generate initial 9 cells
		const playerCellX = gameState.player.x;
		const playerCellY = gameState.player.y;
		seamless = new SeamlessSubworldManager(playerCellX, playerCellY, resolveCell, resolveMode, mapW, mapH, seaLevel);
		await seamless.generateAllAsync();

		// Build composite data from all 9 cells into persistent buffers
		seamless.rebuildComposites();
		const compTrav = seamless.compositeTraversability();
		const compTileGrid = seamless.compositeTileGrid();
		const compHeightmap = seamless.compositeHeightmap();
		const compStructures = seamless.compositeStructures();
		const compVisual = seamless.compositeVisual();

		const traversability: TraversabilityGrid = {
			width: compTrav.width, height: compTrav.height, data: compTrav.data,
		};

		const spawn = seamless.spawnPoint();
		const nextId = {value: 0};
		const entities: SubworldEntity[] = [];
		const rng = xorshift32(seed + 13);

		// Player entity
		entities.push({
			id: nextId.value++, kind: 'player',
			x: spawn.gx, y: spawn.gy, vx: 0, vy: 0,
			radius: 1.5, solid: true, label: 'You', color: '#4af',
			hp: player.combatStats.currentHp, maxHp: player.combatStats.maxHp,
			attackTimer: 0,
		});

		// ── Universal NPC spawning — iterate all cells ──────────
		spawnedCells.clear();
		let citizenSheet: Awaited<ReturnType<typeof createCitizenSpriteSheet>> | undefined;
		for (const cell of seamless.allCells()) {
			spawnedCells.add(`${cell.cx},${cell.cy}`);
			const cellRng = xorshift32(cell.seed + 7);
			const {gx: cellGx, gy: cellGy} = seamless.cellToGlobal(cell.cx, cell.cy, CELL_SIZE / 2, CELL_SIZE / 2);
			const meta = resolveCellMeta(cell.cx, cell.cy);

			// Lazy-create citizen sheet for first city/village
			if ((cell.mode === 'city' || cell.mode === 'village') && !citizenSheet) {
				const found = findSettlementAt(cell.cx, cell.cy);
				if (found) {
					// eslint-disable-next-line no-await-in-loop
					citizenSheet = await createCitizenSpriteSheet(found.population);
				}
			}

			// Build city spot finder for settlements
			let findCitySpot: (() => {x: number; y: number} | undefined) | undefined;
			if (cell.mode === 'city' || cell.mode === 'village') {
				const cellOrigin = seamless.cellToGlobal(cell.cx, cell.cy, 0, 0);
				const allRoads = collectRoadNearHouses(compTileGrid.data, compTileGrid.width, compTileGrid.height);
				const cellPool = allRoads.filter(p =>
					p.x >= cellOrigin.gx && p.x < cellOrigin.gx + CELL_SIZE
					&& p.y >= cellOrigin.gy && p.y < cellOrigin.gy + CELL_SIZE);
				findCitySpot = () => cellPool.length === 0
					? undefined
					: cellPool[Math.floor(cellRng() * cellPool.length)];
			}

			// Universal cell population
			entities.push(...populateCell({
				cellX: cell.cx, cellY: cell.cy,
				biome: meta.biome, feature: meta.feature,
				landmark: meta.landmark, landmarkParam: meta.landmarkParam,
				globalCx: cellGx, globalCy: cellGy,
				seed: cell.seed, traversability,
				macroNpcs: findMacroNpcsAt(cell.cx, cell.cy),
				cityFaction: settlementFaction(cell.cx, cell.cy),
				findCitySpot,
				citizenSheetCount: citizenSheet?.count,
			}, nextId, cellRng));

			// Trade zone + Inn for settlements
			if (cell.mode === 'city' || cell.mode === 'village') {
				const tradeSpot = findTileNear(compTileGrid.data, compTileGrid.width, compTileGrid.height, cellGx, cellGy, TILE_SQUARE, 30);
				if (tradeSpot) {
					entities.push(makeEntity(nextId, {
						kind: 'zone', x: tradeSpot.x + 3, y: tradeSpot.y + 3, radius: 8,
						label: 'Market', color: 'rgba(255,255,100,0.2)', action: {type: 'trade'},
					}));
				}

				const found = findSettlementAt(cell.cx, cell.cy);
				const innSpot = findTileNear(compTileGrid.data, compTileGrid.width, compTileGrid.height, cellGx + 20, cellGy - 15, TILE_ROAD, 40);
				if (innSpot && found) {
					const cost = 'mood' in found && found.mood === 'Prosperous' ? 5 : 10;
					entities.push(makeEntity(nextId, {
						kind: 'zone', x: innSpot.x, y: innSpot.y, radius: 8,
						label: 'Inn', color: 'rgba(100,200,255,0.2)', action: {type: 'rest', cost},
					}));
				}
			}
		}

		if (fightContext) {
			const unitColors: Record<number, string> = {
				0: '#4488ff', 1: '#44cc44', 2: '#aaaa44', 3: '#cc8844',
			};

			const enemyColors: Record<number, string> = {
				0: '#cc4444', 1: '#cc6644', 2: '#884444', 3: '#cc4488',
			};

			entities.push(
				...spawnArmy(fightContext.playerArmy, 'playerArmy', '', unitColors, spawn.gx, spawn.gy, 40, nextId, traversability, rng),
				...spawnArmy(fightContext.enemyArmy, fightContext.enemyFactionId, fightContext.enemyName, enemyColors, spawn.gx + 160, spawn.gy, 40, nextId, traversability, rng),
			);
		}

		const fc = factionContext();
		if (fightContext) {
			const enemyFac = fightContext.enemyFactionId;
			fc.reputation[enemyFac] = -100;
			fc.factions.playerArmy ??= {relations: {}};
			fc.factions.playerArmy.relations[enemyFac] = -100;
			fc.factions[enemyFac] ??= {relations: {}};
			fc.factions[enemyFac].relations.playerArmy = -100;
		}

		currentMapData = seamless.getCenter()?.mapData;
		const name = settlement ? settlement.name : 'The Wilds';
		return {
			seed, width: SEAMLESS_SIZE, height: SEAMLESS_SIZE,
			bgColor: '#1a2a0a', groundColorA: '#2a3a1a', groundColorB: '#253215',
			entities, name,
			bgImage: compVisual, traversability, scale: CITY_SCALE,
			citizenSheet, playerSheet,
			playerDamage: playerDamage(),
			playerRange: 5,
			playerCooldown: 0.5,
			factions: fc.factions,
			playerReputation: fc.reputation,
			heightmap: compHeightmap,
			structures: compStructures,
			tileGrid: compTileGrid.data,
			// Stream loot directly into the player inventory so it appears in the
			// shared inventory overlay during the session (no need to exit first).
			onLoot(gold, items) {
				if (gold > 0) {
					player.gold += gold;
				}

				for (const item of items) {
					addItem(player.inventory, item);
				}

				player.items = player.inventory.items.reduce((s, i) => s + i.quantity, 0);
			},
		};
	}

	// ── Unified exit ────────────────────────────────────────────

	function exitSubworld() {
		if (engine && engine.getDangerLevel() !== 'green') {
			exitBlockedFlash = 1.5;
			return;
		}

		if (seamless) {
			seamless.saveAndClear();
		}

		const result = engine?.getResult();
		if (result) {
			result.playerMp = player.combatStats.currentMp;
			result.playerSp = player.combatStats.currentSp;
		}

		onExit(result);
	}

	// ── Lifecycle ───────────────────────────────────────────────

	onMount(() => {
		// eslint-disable-next-line promise/prefer-await-to-then
		loadTrack('subworld', '/assets/sound/subworld.mp3').then(() => playTrack('subworld')).catch(() => {});

		const configPromise = buildConfig();

		let cancelled = false;

		// eslint-disable-next-line promise/prefer-await-to-then
		configPromise.then(config => {
			if (cancelled) {
				return;
			}

			loading = false;
			engine = new SubworldEngine(config);
			if (fightContext) {
				engine.setFightFactions('player_army', fightContext.enemyFactionId);
			}

			renderer = new SubworldRenderer(canvas);

			// Initialize 3D renderer if heightmap data is available
			if (config.heightmap && config.structures && canvas3d) {
				const rendererSize = SEAMLESS_SIZE;
				renderer3d = new SubworldRenderer3D(canvas3d, rendererSize);
				renderer3d.uploadHeightmap(config.heightmap, config.width, config.height);
				renderer3d.uploadTextureAtlas();
				if (config.tileGrid) {
					renderer3d.uploadTileGrid(config.tileGrid, config.width, config.height);
				}

				if (config.citizenSheet) {
					renderer3d.uploadSpriteSheet(config.citizenSheet.canvas);
				}

				const result = renderer3d.uploadStructures(config.structures);
				renderer3d.uploadStaticBillboards(result.billboards);
				if (seamless) {
					renderer3d.setWaterLevel(seamless.compositeWaterLevel());
				}

				camera = createCamera(engine.player.x, engine.player.y);
				const initFloor = config.structures
					? structureFloorAt(config.structures, camera.x, camera.y, config.heightmap, config.width, config.height, HEIGHT_SCALE)
					: 0;
				updateCameraHeight(camera, config.heightmap, config.width, config.height, false, initFloor);
			}

			let lastTime = performance.now();
			let fpsAccum = 0;
			let fpsFrames = 0;

			// Wire up deferred composite callback — fires after canvas shift
			// and after each cell blit. Only structures (new array each time)
			// and 3D uploads need refreshing; typed-array buffers and canvas
			// are the same references the engine already holds.
			if (seamless) {
				seamless.onCompositesUpdated = region => {
					if (!engine || !seamless) {
						return;
					}

					(engine.config as any).structures = seamless.compositeStructures();

					if (renderer3d) {
						if (region) {
							// Partial update — only one cell changed, upload just that rectangle
							const hm = seamless.compositeHeightmap();
							const tg = seamless.compositeTileGrid();
							renderer3d.uploadHeightmapRegion(hm, SEAMLESS_SIZE, region.ox, region.oy, region.w, region.h);
							renderer3d.uploadTileGridRegion(tg.data, SEAMLESS_SIZE, region.ox, region.oy, region.w, region.h);
						} else {
							// Full update after shift — re-upload entire composite
							const hm = seamless.compositeHeightmap();
							const tg = seamless.compositeTileGrid();
							renderer3d.uploadHeightmap(hm, SEAMLESS_SIZE, SEAMLESS_SIZE);
							renderer3d.uploadTileGrid(tg.data, SEAMLESS_SIZE, SEAMLESS_SIZE);
						}

						renderer3d.setWaterLevel(seamless.compositeWaterLevel());
						const result = renderer3d.uploadStructures((engine.config as any).structures);
						renderer3d.uploadStaticBillboards(result.billboards);
					}
				};

				// Spawn NPCs when a cell becomes ready (preloaded or async)
				seamless.onCellReady = cell => {
					if (!engine || !seamless) {
						return;
					}

					const key = `${cell.cx},${cell.cy}`;
					if (spawnedCells.has(key)) {
						return;
					}

					spawnedCells.add(key);
					// eslint-disable-next-line promise/prefer-await-to-then
					spawnForLoadedCells(engine, seamless, [cell]).catch(() => {});
				};
			}

			function frame(now: number) {
				const dt = (now - lastTime) / 1000;
				lastTime = now;

				// FPS tracking
				const dtMs = dt * 1000;
				fpsAccum += dtMs;
				fpsFrames++;
				if (fpsAccum >= 500) {
					debugFps = (fpsFrames / fpsAccum) * 1000;
					debugFrameDt = fpsAccum / fpsFrames;
					fpsAccum = 0;
					fpsFrames = 0;
				}

				if (engine && !paused && !showDeath && sharedOverlay === undefined) {
					// Input: arrows → camera-relative direction
				// Compute flying state before input so movement can use it
					const isFlying = player.spellBook.sustainedActive.includes('flight');
					let flyDeltaZ = 0;

					if (camera) {
						const forward = (pressed.has('ArrowUp') ? 1 : 0)
							- (pressed.has('ArrowDown') ? 1 : 0);
						const strafe = (pressed.has('ArrowRight') ? 1 : 0)
							- (pressed.has('ArrowLeft') ? 1 : 0);
						if (isFlying) {
							const move = moveVector3d(camera.yaw, camera.pitch, forward, strafe);
							engine.inputDir = {x: move[0], y: move[1]};
							flyDeltaZ = move[2] * engine.playerSpeed * dt;
						} else {
							const move = moveVector(camera.yaw, forward, strafe);
							engine.inputDir = {x: move[0], y: move[1]};
						}
					}

					engine.attackHeld = pressed.has('a') || pressed.has('A');
					engine.playerFlying = isFlying;

					const prevX = engine.player.x;
					const prevY = engine.player.y;

					engine.tick(dt);

					// ── Subworld SP drain — flat rate, no terrain penalty ──
					{
						const movedDx = engine.player.x - prevX;
						const movedDy = engine.player.y - prevY;
						const dist = Math.sqrt(movedDx * movedDx + movedDy * movedDy);
						if (dist > 0.01) {
							distanceAccum += dist;
							const cost = (distanceAccum / 1000) * SUBWORLD_SP_PER_1000;
							if (cost >= 1) {
								const drain = Math.floor(cost);
								distanceAccum -= (drain / SUBWORLD_SP_PER_1000) * 1000;
								const cs = player.combatStats;
								// Overload penalty: extra SP per move when carrying more than capacity.
								const capacity = getCarryCapacity(player.attributes, player.skills);
								const weight = getInventoryWeight(player.inventory);
								const overload = getOverloadPenalty(weight, capacity);
								cs.currentSp -= drain + overload;
								if (cs.currentSp < 0) {
									cs.currentHp += cs.currentSp;
									engine.player.hp = cs.currentHp;
								}
							}
						}
					}

					// ── Seamless boundary check ─────────────────────
					if (seamless) {
						// ── Spire learn check ──
						if (gameState.spires.length > 0) {
							const cellCenterX = CELL_SIZE * 1.5;
							const cellCenterY = CELL_SIZE * 1.5;
							const ddx = engine.player.x - cellCenterX;
							const ddy = engine.player.y - cellCenterY;
							if (ddx * ddx + ddy * ddy < 30 * 30) {
								const cx = ((seamless.centerX % mapW) + mapW) % mapW;
								const cy = ((seamless.centerY % mapH) + mapH) % mapH;
								const spire = gameState.spires.find(s =>
									s.x === cx && s.y === cy && !s.depleted);
								if (spire) {
									spire.depleted = true;
									learnSpell(player.spellBook, spire.spellId);
									const sp = SPELL_LIST.find(s => s.id === spire.spellId);
									showMessage(`You have learned ${sp ? sp.name : spire.spellId}!`);
								}
							}
						}

						// Snapshot entity count before checkBoundary, which may
						// trigger onCellReady and spawn new entities already in
						// the correct coordinate frame.
						const entityCountBefore = engine.entities.length;
						const shift = seamless.checkBoundary(engine.player.x, engine.player.y);
						if (shift) {
							// Translate only pre-existing entities to the new coordinate frame
							const dx = engine.player.x - shift.playerX;
							const dy = engine.player.y - shift.playerY;
							for (let i = 0; i < entityCountBefore; i++) {
								engine.entities[i].x -= dx;
								engine.entities[i].y -= dy;
							}

							// Update spawnedCells: remove cells no longer in the 3×3 grid,
							// record newly loaded cells
							spawnedCells.clear();
							for (const c of seamless.allCells()) {
								spawnedCells.add(`${c.cx},${c.cy}`);
							}

							// Remove entities that shifted outside the 3×3 grid (in-place to preserve array ref)
							cullOutOfBounds(engine);

							// All composite buffers are already shifted synchronously
							// by checkBoundary. Buffer refs are the same — only
							// structures (new array) needs explicit update.
							(engine.config as any).structures = seamless.compositeStructures();

							// 3D renderer uploads deferred to onCompositesUpdated

							// Update center cell map data for save compatibility
							currentMapData = seamless.getCenter()?.mapData;

							// Update location name based on new center cell
							const centerSettlement = findSettlementAt(shift.centerX, shift.centerY);
							centerName = centerSettlement ? centerSettlement.name : 'The Wilds';

							// Notify macroworld of cell change
							onCellChange?.(shift.centerX, shift.centerY);
						}

						// Schedule pre-generation of cells the player approaches
						seamless.tickPreload(engine.player.x, engine.player.y);
					}

					// Sync camera to engine player position (after collision)
					if (camera) {
						camera.x = engine.player.x;
						camera.y = engine.player.y;
						const sFloor = engine.config.structures
							? structureFloorAt(engine.config.structures, camera.x, camera.y, engine.config.heightmap!, engine.config.width, engine.config.height, HEIGHT_SCALE)
							: 0;
						updateCameraHeight(camera, engine.config.heightmap!, engine.config.width, engine.config.height, engine.playerFlying, sFloor, flyDeltaZ);
					}

					tickSpellBook(player.spellBook, player.combatStats, dt, getSpell);

					// Sync player HP back to macroworld state
					if (engine.player.hp !== undefined) {
						player.combatStats.currentHp = engine.player.hp;
					}

					// Death check
					if (player.combatStats.currentHp <= 0) {
						showDeath = true;
					}

					const action = engine.consumeAction();
					if (action) {
						handleAction(action);
					}

					// Count hostiles vs friendlies
					friendlyCount = engine.entities.filter(ent =>
						ent !== engine!.player
						&& ent.kind === 'npc'
						&& (ent.hp ?? 0) > 0
						&& !engine!.isHostileToPlayer(ent)).length;
					enemyCount = engine.entities.filter(ent =>
						ent !== engine!.player
						&& ent.kind === 'npc'
						&& (ent.hp ?? 0) > 0
						&& engine!.isHostileToPlayer(ent)).length;
					dangerLevel = engine.getDangerLevel();
					if (exitBlockedFlash > 0) {
						exitBlockedFlash = Math.max(0, exitBlockedFlash - dt);
					}

					// Refresh combat log view: drop entries older than visibility window
					{
						const log = engine.combatLog;
						let firstVisible = 0;
						for (let li = log.length - 1; li >= 0; li--) {
							if (log[li].time > COMBAT_LOG_VISIBLE_SECONDS) {
								firstVisible = li + 1;
								break;
							}
						}

						const slice = log.slice(firstVisible).slice(-COMBAT_LOG_MAX_LINES);
						combatLogView = slice.map(entry => ({text: entry.text, age: entry.time}));
					}

					if (renderer3d && camera) {
						// 3D render path
						const npcBillboards: BillboardEntity[] = [];
						const sheet = engine.config.citizenSheet;
						for (const entity of engine.entities) {
							if (entity === engine.player || entity.kind === 'zone' || entity.kind === 'projectile') {
								continue;
							}

							if ((entity.hp ?? 0) <= 0 && entity.kind === 'npc') {
								continue;
							}

							const [r, g, b] = parseHexColor(entity.color);
							// Compute sprite UV from citizen sheet if available
							let spriteUv: [number, number, number, number] | undefined;
							if (entity.spriteIndex !== undefined && sheet) {
								const dirIndex = 0; // Front-facing
								const frame = entity.animFrame ?? 0;
								const cellX = dirIndex * sheet.framesPerDirection + frame;
								const cellY = entity.spriteIndex;
								const su = cellX * sheet.spriteSize / sheet.canvas.width;
								const sv = cellY * sheet.spriteSize / sheet.canvas.height;
								const sw = sheet.spriteSize / sheet.canvas.width;
								const sh = sheet.spriteSize / sheet.canvas.height;
								spriteUv = [su, sv, sw, sh];
							}

							npcBillboards.push({
								x: entity.x, z: entity.y,
								width: 2, height: 2,
								r, g, b, a: 1,
								spriteUv,
							});
						}

						renderer3d.uploadBillboards(npcBillboards);
						const aspect = canvas3d.clientWidth / (canvas3d.clientHeight || 1);
						canvas3d.width = canvas3d.clientWidth * (window.devicePixelRatio || 1);
						canvas3d.height = canvas3d.clientHeight * (window.devicePixelRatio || 1);
						renderer3d.render(camera, aspect, gameState.worldTime, gameState.seed);

						// Minimap: render 2D view into minimap canvas (~100 tile radius)
						if (!showLargeMap && renderer) {
							const dpr = window.devicePixelRatio || 1;
							const minimapScale = (canvas.clientWidth * dpr) / 200;
							renderer.render(engine.config, engine.player.x, engine.player.y, minimapScale, player.spellBook.sustainedActive);

							// Draw direction arrow on minimap
							const mctx = canvas.getContext('2d');
							if (mctx) {
								const mw = canvas.width;
								const mh = canvas.height;
								const cx = mw / 2;
								const cy = mh / 2;
								const arrowLen = Math.min(mw, mh) * 0.18;
								const ax = Math.cos(camera.yaw);
								const ay = Math.sin(camera.yaw);
								mctx.save();
								mctx.strokeStyle = 'rgba(255, 50, 50, 0.9)';
								mctx.fillStyle = 'rgba(255, 50, 50, 0.9)';
								mctx.lineWidth = 2.5;
								mctx.beginPath();
								mctx.moveTo(cx, cy);
								mctx.lineTo(cx + ax * arrowLen, cy + ay * arrowLen);
								mctx.stroke();
								const headLen = arrowLen * 0.3;
								const headAngle = 0.45;
								const tipX = cx + ax * arrowLen;
								const tipY = cy + ay * arrowLen;
								mctx.beginPath();
								mctx.moveTo(tipX, tipY);
								mctx.lineTo(
									tipX - headLen * Math.cos(camera.yaw - headAngle),
									tipY - headLen * Math.sin(camera.yaw - headAngle),
								);
								mctx.lineTo(
									tipX - headLen * Math.cos(camera.yaw + headAngle),
									tipY - headLen * Math.sin(camera.yaw + headAngle),
								);
								mctx.closePath();
								mctx.fill();
								mctx.restore();
							}
						}

						// Large 2D map (toggle with M) — full center cell 1024×1024
						if (showLargeMap && largeMapCanvas) {
							largeMapRenderer ||= new SubworldRenderer(largeMapCanvas);

							const dpr = window.devicePixelRatio || 1;
							const mapPixels = largeMapCanvas.clientWidth * dpr;
							const mapScale = mapPixels / CELL_SIZE;
							const centerX = CELL_SIZE + CELL_SIZE / 2;
							const centerY = CELL_SIZE + CELL_SIZE / 2;
							largeMapRenderer.render(engine.config, centerX, centerY, mapScale, player.spellBook.sustainedActive);

							// Draw player arrow at correct position on the large map
							const lctx = largeMapCanvas.getContext('2d');
							if (lctx) {
								const mw = largeMapCanvas.width;
								const mh = largeMapCanvas.height;
								const px = mw / 2 + (engine.player.x - centerX) * mapScale;
								const py = mh / 2 + (engine.player.y - centerY) * mapScale;
								const arrowLen = Math.min(mw, mh) * 0.03;
								const ax = Math.cos(camera.yaw);
								const ay = Math.sin(camera.yaw);
								lctx.save();
								lctx.strokeStyle = 'rgba(255, 50, 50, 0.9)';
								lctx.fillStyle = 'rgba(255, 50, 50, 0.9)';
								lctx.lineWidth = 2.5;
								lctx.beginPath();
								lctx.moveTo(px, py);
								lctx.lineTo(px + ax * arrowLen, py + ay * arrowLen);
								lctx.stroke();
								const headLen = arrowLen * 0.3;
								const headAngle = 0.45;
								const tipX = px + ax * arrowLen;
								const tipY = py + ay * arrowLen;
								lctx.beginPath();
								lctx.moveTo(tipX, tipY);
								lctx.lineTo(
									tipX - headLen * Math.cos(camera.yaw - headAngle),
									tipY - headLen * Math.sin(camera.yaw - headAngle),
								);
								lctx.lineTo(
									tipX - headLen * Math.cos(camera.yaw + headAngle),
									tipY - headLen * Math.sin(camera.yaw + headAngle),
								);
								lctx.closePath();
								lctx.fill();
								lctx.restore();
							}
						}
					}
				}

				if (lowSpMsgCooldown > 0) {
					lowSpMsgCooldown -= dt;
				}

				// Trigger HUD damage flash on any HP drop (combat + exhaustion)
				if (engine) {
					const curHp = engine.player.hp ?? 0;
					if (curHp < lastPlayerHp - 0.01) {
						hitFlashTimer = Math.max(hitFlashTimer, 0.3);
					}

					lastPlayerHp = curHp;
				}

				if (hitFlashTimer > 0) {
					hitFlashTimer = Math.max(0, hitFlashTimer - dt);
				}

				if (message && messageTimer > 0) {
					messageTimer -= dt;
					if (messageTimer <= 0) {
						message = '';
					}
				}

				animFrame = requestAnimationFrame(frame);
			}

			animFrame = requestAnimationFrame(frame);
		// eslint-disable-next-line promise/prefer-await-to-then
		}).catch((error: unknown) => {
			console.error('[SubworldScreen] buildConfig failed:', error);
			loading = false;
		});

		return () => {
			cancelled = true;
			cancelAnimationFrame(animFrame);
			renderer3d?.dispose();
			playTrack('explore');
		};
	});

	function handleAction(action: ZoneAction) {
		switch (action.type) {
			case 'exit': {
				exitSubworld();
				break;
			}

			case 'trade': {
				onTrade();
				break;
			}

			case 'rest': {
				if (player.gold < action.cost) {
					showMessage(`Not enough gold! (need ${action.cost}g)`);
					break;
				}

				player.gold -= action.cost;
				player.combatStats.currentHp = player.combatStats.maxHp;
				player.combatStats.currentMp = player.combatStats.maxMp;
				player.combatStats.currentSp = player.combatStats.maxSp;
				showMessage(`Rested! HP/MP/SP restored. (-${action.cost}g)`);
				break;
			}

			case 'dialog': {
				showMessage(action.text);
				break;
			}

			default: {
				break;
			}
		}
	}

	function showMessage(text: string) {
		message = text;
		messageTimer = 3;
	}

	function handleKeyDown(event: KeyboardEvent) {
		if (event.key === '`') {
			showDebug = !showDebug;
			return;
		}

		if (event.key === 'Escape') {
			if (showDebug) {
				showDebug = false;
				return;
			}

			if (sharedOverlay !== undefined) {
				sharedOverlay = undefined;
				return;
			}

			exitSubworld();
			return;
		}

		// Shared overlays (inventory, spells, ...) — same keys as macroworld.
		const sharedId = sharedOverlayForKey(event.key);
		if (sharedId !== undefined) {
			if (sharedOverlay === undefined && document.pointerLockElement) {
				document.exitPointerLock();
			}

			sharedOverlay = toggleSharedOverlay(sharedOverlay, sharedId);
			return;
		}

		// Swallow gameplay input while a shared overlay is open.
		if (sharedOverlay !== undefined) {
			return;
		}

		if (event.key === ' ') {
			event.preventDefault();
			paused = !paused;
			return;
		}

		if ((event.key === 's' || event.key === 'S') && engine) {
			tryCastSpell();
			return;
		}

		// Toggle large 2D map
		if (event.key === 'm' || event.key === 'M') {
			showLargeMap = !showLargeMap;
			return;
		}

		// Escape pointer lock
		if (event.key === 'Escape' && document.pointerLockElement === canvas3d) {
			document.exitPointerLock();
			return;
		}

		// Prevent arrow keys from scrolling the page
		if (event.key.startsWith('Arrow')) {
			event.preventDefault();
		}

		pressed.add(event.key);
	}

	function handleKeyUp(event: KeyboardEvent) {
		pressed.delete(event.key);
	}

	function handleMouseMove(event: MouseEvent) {
		if (!engine) {
			return;
		}

		// Pointer lock: mouse movement rotates camera
		if (camera && document.pointerLockElement === canvas3d) {
			rotateCamera(camera, event.movementX, event.movementY);
		}
	}

	function handleCanvasClick() {
		if (canvas3d && document.pointerLockElement !== canvas3d) {
			canvas3d.requestPointerLock();
		}
	}

	function tryCastSpell() {
		if (!engine || !activeSpell) {
			return;
		}

		const check = canCast(activeSpell, player.combatStats, player.spellBook, true);
		if (!check.ok) {
			showMessage(check.reason);
			return;
		}

		// Sustained / self spells toggle without spawning projectiles
		if (activeSpell.sustained || activeSpell.micro?.shape === 'self') {
			startCast(activeSpell, player.combatStats, player.spellBook);
			return;
		}

		const dmg = spellDamage(activeSpell, player.attributes, player.skills);
		const rad = activeSpell.micro?.baseRadius ?? 1;
		const projSpeed = activeSpell.micro?.speed ?? 300;
		const blast = activeSpell.micro?.friendlyFire ? rad : 0;

		// Tag colors by first spell tag
		const tagColors: Record<string, string> = {
			fire: '#ff6633',
			ice: '#66ccff',
			lightning: '#ffee44',
			arcane: '#bb88ff',
			light: '#ffffaa',
			dark: '#884488',
			air: '#aaddff',
			body: '#66ff88',
			earth: '#aa8844',
			mind: '#ff88dd',
		};
		const spellColor = tagColors[activeSpell.tags[0]] ?? '#bb88ff';

		const projRadius = Math.max(1, rad > 10 ? 2.5 : 1.5);
		// Compute target point from camera direction
		const targetX = camera ? engine.player.x + Math.sin(camera.yaw) * 100 : mouseWorldX;
		const targetY = camera ? engine.player.y - Math.cos(camera.yaw) * 100 : mouseWorldY;
		const cast = engine.castSpell(dmg, projSpeed, projRadius, blast, activeSpell.micro?.friendlyFire ?? false, targetX, targetY, spellColor, activeSpell.id);
		if (cast) {
			startCast(activeSpell, player.combatStats, player.spellBook);
		}
	}

	// ── Debug cheat handlers ───────────────────────────────────

	function debugHealPlayer() {
		player.combatStats.currentHp = player.combatStats.maxHp;
		player.combatStats.currentMp = player.combatStats.maxMp;
		player.combatStats.currentSp = player.combatStats.maxSp;
		if (engine?.player) {
			engine.player.hp = player.combatStats.maxHp;
			engine.player.maxHp = player.combatStats.maxHp;
		}
	}

	function debugLearnAllSpells() {
		for (const spell of SPELL_LIST) {
			learnSpell(player.spellBook, spell.id);
		}
	}

	function debugAddExp(amount: number) {
		player.levelData.exp += amount;
		while (tryLevelUp(player.levelData)) {
			// Level up as many times as needed
		}
	}
</script>

<svelte:window onkeydown={handleKeyDown} onkeyup={handleKeyUp} />

<div class="absolute inset-0 z-100 flex flex-col" style="background: #1a1a1a;">
	<!-- HUD -->
	<div class="flex items-center justify-between bg-black/80 px-4 py-2 font-sans text-sm">
		<div class="flex items-center gap-4">
			<!-- Danger gem (M&M 6/7/8 style) -->
			<span
				class="inline-block h-5 w-5 rounded-full border border-black/60"
				title={gemTitle}
				style="background: radial-gradient(circle at 30% 30%, #fff8, {gemColor} 60%, #000a 100%); box-shadow: 0 0 8px {gemColor};"
			></span>
			<span class="font-bold uppercase tracking-wider" style="color: {color.accent};">{locationName}</span>
			<span style="color: {color.hp};">HP: {fmtStat(player.combatStats.currentHp)}/{player.combatStats.maxHp}</span>
			<span style="color: {color.mp};">MP: {fmtStat(player.combatStats.currentMp)}/{player.combatStats.maxMp}</span>
			<span style="color: {color.sp};">SP: {fmtStat(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
			<span class="text-yellow-400">Gold: {player.gold}</span>
			{#if friendlyCount > 0}
				<span class="text-green-400">Allies: {friendlyCount}</span>
			{/if}
			{#if enemyCount > 0}
				<span class="text-red-400">Enemies: {enemyCount}</span>
			{/if}
			{#if exitBlockedFlash > 0}
				<span class="text-red-300">Cannot leave — enemies nearby!</span>
			{/if}
		</div>
		<button onclick={exitSubworld}
			class="rounded border-2 px-3 py-1 text-xs font-bold uppercase tracking-wide transition disabled:cursor-not-allowed disabled:opacity-50"
			disabled={dangerLevel !== 'green'}
			{...btnProps('close')}
		>Leave [Esc]</button>
	</div>

	<!-- Canvas -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div class="relative flex-1" onmousemove={handleMouseMove} onclick={handleCanvasClick}>
		<canvas bind:this={canvas3d} class="h-full w-full"></canvas>
		<!-- 2D minimap (always visible, corner) -->
		<canvas
			bind:this={canvas}
			class="pointer-events-none absolute right-4 top-4 h-48 w-48 rounded-full border-2 border-white/30 shadow-lg"
			class:hidden={showLargeMap}
			style="image-rendering: pixelated; object-fit: cover;"
		></canvas>
		<!-- Large 2D map overlay (toggle with M) — shows full center cell 1024×1024 -->
		<div class="absolute inset-0 flex items-center justify-center bg-black/60" class:hidden={!showLargeMap}>
			<canvas
				class="pointer-events-none rounded border-2 border-white/30 shadow-lg"
				bind:this={largeMapCanvas}
				style="image-rendering: pixelated; width: min(90vw, 90vh); height: min(90vw, 90vh);"
			></canvas>
		</div>

		{#if loading}
			<div class="absolute inset-0 flex items-center justify-center bg-black/80">
				<span class="font-sans text-sm" style={mutedStyle}>Generating world...</span>
			</div>
		{/if}

		{#if paused && !loading}
			<div class="absolute inset-0 flex items-center justify-center bg-black/50">
				<span class="font-sans text-2xl font-bold" style="color: #ffd700; text-shadow: 0 2px 8px rgba(0,0,0,0.7);">PAUSED</span>
			</div>
		{/if}

		<div class="pointer-events-none absolute bottom-4 left-4 rounded bg-black/60 px-3 py-2 font-sans text-xs" style={mutedStyle}>
			Click to look · Arrows to move · A attack · S spell · I inventory · B spellbook · M map · Space pause
		</div>

		{#if activeSpell}
			<div class="pointer-events-none absolute bottom-4 right-4 rounded bg-black/60 px-3 py-2 text-right font-sans text-xs" style={mutedStyle}>
				<span style="color: #ffd700;">{activeSpell.name}</span>
				{#if activeSpell.sustained && player.spellBook.sustainedActive.includes(activeSpell.id)}
					<span style="color: #66ccff;"> (active \u2022 {activeSpell.manaDrain}/s)</span>
				{:else if player.spellBook.cooldowns[activeSpell.id] > 0}
					<span style="color: #ff6644;"> (cd {player.spellBook.cooldowns[activeSpell.id].toFixed(1)}s)</span>
				{/if}
			</div>
		{/if}

		{#if message}
			<div
				class="pointer-events-none absolute left-1/2 top-24 -translate-x-1/2 rounded border-2 px-5 py-2 text-center font-sans text-base font-bold shadow-2xl"
				style={messageStyle}
			>
				{message}
			</div>
		{/if}

		{#if hitFlashTimer > 0}
			<div
				class="pointer-events-none absolute inset-0"
				style="background: rgba(220, 40, 40, {Math.min(0.45, hitFlashTimer * 1.6)}); mix-blend-mode: multiply;"
			></div>
		{/if}

		{#if hitFlashTimer > 0}
			<div
				class="pointer-events-none absolute inset-0"
				style="background: rgba(220, 40, 40, {Math.min(0.45, hitFlashTimer * 1.6)}); mix-blend-mode: multiply;"
			></div>
		{/if}

		{#if combatLogView.length > 0}
			<div class="pointer-events-none absolute left-1/2 top-16 flex -translate-x-1/2 flex-col items-center gap-1 font-sans text-sm">
				{#each combatLogView as entry (entry.text + entry.age)}
					<div
						class="rounded bg-black/60 px-3 py-1 shadow"
						style="color: #ffd6a8; opacity: {Math.max(0.15, 1 - entry.age / COMBAT_LOG_VISIBLE_SECONDS)};"
					>{entry.text}</div>
				{/each}
			</div>
		{/if}
	</div>

	<!-- Debug overlay -->
	{#if showDebug}
		<DebugOverlay
			data={{
				gState: gameState,
				npcs: [],
				cityNpcs: [],
				inCity: Boolean(settlement),
				trees: [],
				mapW: SEAMLESS_SIZE,
				mapH: SEAMLESS_SIZE,
				visualPlayerX: engine?.player.x ?? 0,
				visualPlayerY: engine?.player.y ?? 0,
				fps: debugFps,
				frameDt: debugFrameDt,
				simSpeed: paused ? 0 : 1,
				zoom: 1,
				cameraX: engine?.player.x ?? 0,
				cameraY: engine?.player.y ?? 0,
				canvasW: canvas?.width ?? 0,
				canvasH: canvas?.height ?? 0,
				dpr: globalThis.window === undefined ? 1 : (window.devicePixelRatio || 1),
				atlasUploaded: false,
				subworld: {
					entities: engine?.entities.map(e => ({
						id: e.id, kind: e.kind, x: e.x, y: e.y,
						hp: e.hp, maxHp: e.maxHp, label: e.label, factionId: e.factionId,
					})) ?? [],
					playerX: engine?.player.x ?? 0,
					playerY: engine?.player.y ?? 0,
					mode,
					showMinimap: !showLargeMap,
					friendlies: friendlyCount,
					enemies: enemyCount,
					seed,
					mapSize: SEAMLESS_SIZE,
				},
			}}
			onClose={() => {
				showDebug = false;
			}}
			onHealPlayer={debugHealPlayer}
			onLearnAllSpells={debugLearnAllSpells}
			onAddExp={debugAddExp}
			onKillAllEnemies={() => {
				if (!engine) {
					return;
				}

				for (const ent of engine.entities) {
					if (ent !== engine.player && ent.kind === 'npc' && engine.isHostileToPlayer(ent)) {
						ent.hp = 0;
					}
				}
			}}
		/>
	{/if}

	{#if showDeath}
		<DeathOverlay
			onRestart={onNewGame}
			onReborn={onReborn}
			onMainMenu={onBackToTitle}
		/>
	{/if}

	<!-- Shared overlays (inventory, spells) — also available in macroworld. -->
	<SharedOverlays
		bind:player
		{gameState}
		inMicro={true}
		bind:open={sharedOverlay}
	/>
</div>

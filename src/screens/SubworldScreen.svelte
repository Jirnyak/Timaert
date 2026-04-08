<script lang="ts">
	import {onMount} from 'svelte';
	import type {PlayerState, GameState, AnySettlement} from '../game/state';
	import {
		SubworldEngine, SubworldRenderer, SubworldRenderer3D,
		findWalkable, makeEntity,
		createCitizenSpriteSheet, renderPlayerSprite,
		spawnArmy, spawnCityNpcs, spawnWildernessNpcs,
		type SubworldConfig, type SubworldEntity, type TraversabilityGrid,
		type ZoneAction, type SubworldResult, type FightContext,
		type BillboardEntity,
		createCamera, updateCameraHeight, rotateCamera, moveVector,
		type CameraState,
		SeamlessSubworldManager, CELL_SIZE,
		type CellResolver, type ModeResolver, type LoadedCell,
	} from '../game/subworld';
	import {xorshift32} from '../game/rng';
	import {
		type SubworldMode, type MapData,
		TILE_ROAD, TILE_SQUARE, TILE_WATER, TILE_SHORE,
		findTileNear, collectRoadNearHouses,
		CellFeature, Biome,
	} from '../game/subworld/map-data';
	import {SUBWORLD_SP_PER_1000, SUBWORLD_WATER_SP_PER_1000} from '../game/movement-cost';
	import {biomeFromClimate} from '../game/biomes';
	import {NPCType, settlementFaction} from '../game/npc';
	import {calculateDerived, tryLevelUp} from '../game/attributes';
	import {loadTrack, playTrack} from '../game/audio';
	import {
		getSpell, canCast, startCast, tickSpellBook, spellDamage, spellRadius,
		SPELL_LIST, learnSpell,
	} from '../game/spells';
	import {
		color, btnProps, messageStyle, mutedStyle,
	} from '../ui/theme';
	import {FeatureType, getFeatureAt, type FeatureLayer} from '../game/features';
	import DebugOverlay from './DebugOverlay.svelte';

	type Props = {
		player: PlayerState;
		gameState: GameState;
		settlement?: AnySettlement;
		seed: number;
		mode: SubworldMode;
		fightContext?: FightContext;
		onExit: (result?: SubworldResult) => void;
		onTrade: () => void;
		/** Macroworld feature layer for seamless mode. */
		featureLayer?: FeatureLayer;
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
	};

	let {
		player = $bindable(), gameState, settlement, seed, mode,
		fightContext, onExit, onTrade,
		featureLayer, macroHeightData,
		macroMoistureData, macroTemperatureData,
		mapW = 512, mapH = 512,
		seaLevel = 0.4,
		onCellChange,
	}: Props = $props();

	let canvas: HTMLCanvasElement;
	let canvas3d: HTMLCanvasElement;
	let message = $state('');
	let messageTimer = 0;
	let engine: SubworldEngine | undefined;
	let renderer: SubworldRenderer | undefined;
	let renderer3d: SubworldRenderer3D | undefined;
	let camera: CameraState | undefined;
	let currentMapData: MapData | undefined;
	let animFrame = 0;
	let loading = $state(true);
	let zoom = $state(1);
	let view3d = $state(true);
	let friendlyCount = $state(0);
	let enemyCount = $state(0);
	let paused = $state(false);
	let showDebug = $state(false);
	let debugFps = $state(0);
	let debugFrameDt = $state(0);

	// Spell casting state
	let mouseWorldX = 0;
	let mouseWorldY = 0;

	/** Seamless manager — used for nature/wilds mode. */
	let seamless: SeamlessSubworldManager | undefined;

	/** Accumulated subworld distance for SP drain (resets every 1000 units). */
	let distanceAccum = 0;

	const activeSpell = $derived(getSpell(player.spellBook.activeSpellId));

	const ZOOM_MIN = 0.25;
	const ZOOM_MAX = 4;
	const ZOOM_STEP = 1.15;
	const MAP_SIZE = 1024;
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
			const nextId = {value: 0}; // IDs discarded — engine.addEntity assigns real IDs

			if (cell.mode === 'city' || cell.mode === 'village') {
				const found = findSettlementAt(cell.cx, cell.cy);
				if (!found) {
					continue;
				}

				// Ensure citizenSheet exists — create on demand if entering first city
				if (!eng.config.citizenSheet) {
					// eslint-disable-next-line no-await-in-loop
					const sheet = await createCitizenSpriteSheet(found.population);
					(eng.config as any).citizenSheet = sheet;
					if (renderer3d) {
						renderer3d.uploadSpriteSheet(sheet.canvas);
					}
				}

				const sheet = eng.config.citizenSheet!;

				const faction = settlementFaction(cell.cx, cell.cy);
				const npcDistribution: Array<{type: NPCType; weight: number}> = [
					{type: NPCType.Peasant, weight: 0.55},
					{type: NPCType.Merchant, weight: 0.2},
					{type: NPCType.Woodcutter, weight: 0.2},
					{type: NPCType.Witch, weight: 0.05},
					{type: NPCType.Guard, weight: 0},
					{type: NPCType.Sorceress, weight: 0},
				];
				const guardTypes = new Set([NPCType.Guard, NPCType.Sorceress]);

				const cellOrigin = sm.cellToGlobal(cell.cx, cell.cy, 0, 0);
				const allRoads = collectRoadNearHouses(compTileGrid.data, compTileGrid.width, compTileGrid.height);
				const cellPool = allRoads.filter(p =>
					p.x >= cellOrigin.gx && p.x < cellOrigin.gx + CELL_SIZE
					&& p.y >= cellOrigin.gy && p.y < cellOrigin.gy + CELL_SIZE);

				const spawned = spawnCityNpcs(found.population, faction, npcDistribution, guardTypes, nextId, cellRng, () => cellPool.length === 0
					? undefined
					: cellPool[Math.floor(cellRng() * cellPool.length)], sheet.count);
				for (const ent of spawned) {
					const {id: _, ...rest} = ent;
					eng.addEntity(rest as any);
				}

				// Trade zone
				const tradeSpot = findTileNear(compTileGrid.data, compTileGrid.width, compTileGrid.height, cellGx, cellGy, TILE_SQUARE, 30);
				if (tradeSpot) {
					eng.addEntity({
						kind: 'zone', x: tradeSpot.x + 3, y: tradeSpot.y + 3, radius: 8,
						label: 'Market', color: 'rgba(255,255,100,0.2)', action: {type: 'trade'},
					} as any);
				}

				// Inn
				const innSpot = findTileNear(compTileGrid.data, compTileGrid.width, compTileGrid.height, cellGx + 20, cellGy - 15, TILE_ROAD, 40);
				if (innSpot) {
					const cost = 'mood' in found && found.mood === 'Prosperous' ? 5 : 10;
					eng.addEntity({
						kind: 'zone', x: innSpot.x, y: innSpot.y, radius: 8,
						label: 'Inn', color: 'rgba(100,200,255,0.2)', action: {type: 'rest', cost},
					} as any);
				}
			} else {
				// Wilderness: spawn some creatures per cell
				const creatureNames = ['Deer', 'Wolf', 'Rabbit', 'Fox', 'Bear'];
				const count = 1 + Math.floor(cellRng() * 3);
				for (let i = 0; i < count; i++) {
					const spot = findWalkable(traversability, cellRng, cellGx, cellGy, CELL_SIZE / 3);
					if (spot) {
						eng.addEntity({
							kind: 'npc', x: spot.x, y: spot.y, radius: 0.5, solid: true,
							label: creatureNames[Math.floor(cellRng() * creatureNames.length)],
							color: `hsl(${Math.floor(cellRng() * 120)}, 35%, 45%)`,
							ai: 'wander', aiTimer: cellRng() * 3,
						} as any);
					}
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

			let landmark: 'city' | 'village' | 'ruin' | undefined;
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
		let citizenSheet: Awaited<ReturnType<typeof createCitizenSpriteSheet>> | undefined;
		for (const cell of seamless.allCells()) {
			const cellRng = xorshift32(cell.seed + 7);
			const {gx: cellGx, gy: cellGy} = seamless.cellToGlobal(cell.cx, cell.cy, CELL_SIZE / 2, CELL_SIZE / 2);

			if (cell.mode === 'city' || cell.mode === 'village') {
				// Find the actual settlement at this macro position
				const found = findSettlementAt(cell.cx, cell.cy);
				if (!found) {
					continue;
				}

				// eslint-disable-next-line no-await-in-loop
				citizenSheet ||= await createCitizenSpriteSheet(found.population);

				const faction = settlementFaction(cell.cx, cell.cy);
				const npcDistribution: Array<{type: NPCType; weight: number}> = [
					{type: NPCType.Peasant, weight: 0.55},
					{type: NPCType.Merchant, weight: 0.2},
					{type: NPCType.Woodcutter, weight: 0.2},
					{type: NPCType.Witch, weight: 0.05},
					{type: NPCType.Guard, weight: 0},
					{type: NPCType.Sorceress, weight: 0},
				];
				const guardTypes = new Set([NPCType.Guard, NPCType.Sorceress]);

				// Spawn pool: roads near houses in this cell's area of the composite
				const cellOrigin = seamless.cellToGlobal(cell.cx, cell.cy, 0, 0);
				const allRoads = collectRoadNearHouses(compTileGrid.data, compTileGrid.width, compTileGrid.height);
				const cellPool = allRoads.filter(p =>
					p.x >= cellOrigin.gx && p.x < cellOrigin.gx + CELL_SIZE
					&& p.y >= cellOrigin.gy && p.y < cellOrigin.gy + CELL_SIZE);

				entities.push(...spawnCityNpcs(found.population, faction, npcDistribution, guardTypes, nextId, cellRng, () => cellPool.length === 0
					? undefined
					: cellPool[Math.floor(cellRng() * cellPool.length)], citizenSheet.count));

				// Trade zone
				const tradeSpot = findTileNear(compTileGrid.data, compTileGrid.width, compTileGrid.height, cellGx, cellGy, TILE_SQUARE, 30);
				if (tradeSpot) {
					entities.push(makeEntity(nextId, {
						kind: 'zone', x: tradeSpot.x + 3, y: tradeSpot.y + 3, radius: 8,
						label: 'Market', color: 'rgba(255,255,100,0.2)', action: {type: 'trade'},
					}));
				}

				// Inn
				const innSpot = findTileNear(compTileGrid.data, compTileGrid.width, compTileGrid.height, cellGx + 20, cellGy - 15, TILE_ROAD, 40);
				if (innSpot) {
					const cost = 'mood' in found && found.mood === 'Prosperous' ? 5 : 10;
					entities.push(makeEntity(nextId, {
						kind: 'zone', x: innSpot.x, y: innSpot.y, radius: 8,
						label: 'Inn', color: 'rgba(100,200,255,0.2)', action: {type: 'rest', cost},
					}));
				}
			} else {
				// Wilderness: spawn some creatures per cell
				const creatureNames = ['Deer', 'Wolf', 'Rabbit', 'Fox', 'Bear'];
				const count = 1 + Math.floor(cellRng() * 3);
				for (let i = 0; i < count; i++) {
					const spot = findWalkable(traversability, cellRng, cellGx, cellGy, CELL_SIZE / 3);
					if (spot) {
						entities.push(makeEntity(nextId, {
							kind: 'npc', x: spot.x, y: spot.y, radius: 0.5, solid: true,
							label: creatureNames[Math.floor(cellRng() * creatureNames.length)],
							color: `hsl(${Math.floor(cellRng() * 120)}, 35%, 45%)`,
							ai: 'wander', aiTimer: cellRng() * 3,
						}));
					}
				}
			}
		}

		// Some bandits around the player's spawn region
		const banditCount = 3 + Math.floor(rng() * 5);
		entities.push(...spawnWildernessNpcs(NPCType.Bandit, banditCount, 'cults', '#cc4444', nextId, traversability, rng, spawn.gx + 80, spawn.gy, 150));
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
		};
	}

	// ── Unified exit ────────────────────────────────────────────

	function exitSubworld() {
		if (seamless) {
			seamless.saveAndClear();
		}

		const result = engine?.getResult();
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
				updateCameraHeight(camera, config.heightmap, config.width, config.height, false);
			}

			let lastTime = performance.now();
			let fpsAccum = 0;
			let fpsFrames = 0;

			// Wire up deferred composite callback — fires after canvas shift
			// and after each cell blit. Only structures (new array each time)
			// and 3D uploads need refreshing; typed-array buffers and canvas
			// are the same references the engine already holds.
			if (seamless) {
				seamless.onCompositesUpdated = () => {
					if (!engine || !seamless) {
						return;
					}

					(engine.config as any).structures = seamless.compositeStructures();

					if (renderer3d) {
						const hm = seamless.compositeHeightmap();
						const tg = seamless.compositeTileGrid();
						renderer3d.uploadHeightmap(hm, SEAMLESS_SIZE, SEAMLESS_SIZE);
						renderer3d.uploadTileGrid(tg.data, SEAMLESS_SIZE, SEAMLESS_SIZE);
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

				if (engine && !paused) {
					// Input: 3D uses arrows → camera-relative direction; 2D uses arrows → world direction
					if (view3d && camera) {
						const forward = (pressed.has('ArrowUp') ? 1 : 0)
							- (pressed.has('ArrowDown') ? 1 : 0);
						const strafe = (pressed.has('ArrowRight') ? 1 : 0)
							- (pressed.has('ArrowLeft') ? 1 : 0);
						const move = moveVector(camera.yaw, forward, strafe);
						// Feed camera-relative direction into engine — it handles collision
						engine.inputDir = {x: move[0], y: move[1]};
					} else {
						engine.inputDir = {
							x: (pressed.has('ArrowRight') ? 1 : 0)
								- (pressed.has('ArrowLeft') ? 1 : 0),
							y: (pressed.has('ArrowDown') ? 1 : 0)
								- (pressed.has('ArrowUp') ? 1 : 0),
						};
					}

					engine.attackHeld = pressed.has('a') || pressed.has('A');
					engine.playerFlying = player.spellBook.sustainedActive.includes('flight');

					const prevX = engine.player.x;
					const prevY = engine.player.y;

					engine.tick(dt);

					// ── Subworld SP drain based on distance ─────────
					{
						const movedDx = engine.player.x - prevX;
						const movedDy = engine.player.y - prevY;
						const dist = Math.sqrt(movedDx * movedDx + movedDy * movedDy);
						if (dist > 0.01) {
							// Determine cost rate from tile type at player position
							const tg = engine.config.tileGrid;
							const tw = engine.config.width;
							const gx = Math.floor(engine.player.x);
							const gy = Math.floor(engine.player.y);
							let tile = 0;
							if (tg && gx >= 0 && gx < tw && gy >= 0 && gy < engine.config.height) {
								tile = tg[gy * tw + gx];
							}

							const isWater = tile === TILE_WATER || tile === TILE_SHORE;
							const spPer1000 = isWater ? SUBWORLD_WATER_SP_PER_1000 : SUBWORLD_SP_PER_1000;

							distanceAccum += dist;
							const cost = (distanceAccum / 1000) * spPer1000;
							if (cost >= 1) {
								const drain = Math.floor(cost);
								distanceAccum -= (drain / spPer1000) * 1000;
								const cs = player.combatStats;
								cs.currentSp -= drain;
								if (cs.currentSp < 0) {
									cs.currentHp += cs.currentSp; // Negative → HP loss
									cs.currentSp = 0;
								}
							}
						}
					}

					// ── Seamless boundary check ─────────────────────
					if (seamless) {
						const shift = seamless.checkBoundary(engine.player.x, engine.player.y);
						if (shift) {
							// Translate all entities to the new coordinate frame
							const dx = engine.player.x - shift.playerX;
							const dy = engine.player.y - shift.playerY;
							for (const ent of engine.entities) {
								ent.x -= dx;
								ent.y -= dy;
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
					if (view3d && camera) {
						camera.x = engine.player.x;
						camera.y = engine.player.y;
						updateCameraHeight(camera, engine.config.heightmap!, engine.config.width, engine.config.height, engine.playerFlying);
					}

					tickSpellBook(player.spellBook, player.combatStats, dt, getSpell);

					// Sync player HP back to macroworld state
					if (engine.player.hp !== undefined) {
						player.combatStats.currentHp = engine.player.hp;
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

					if (view3d && renderer3d && camera) {
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
						renderer3d.render(camera, aspect);

						// Minimap: render 2D view into minimap canvas (~100 tile radius)
						if (renderer) {
							const dpr = window.devicePixelRatio || 1;
							const minimapScale = (canvas.clientWidth * dpr) / 200;
							renderer.render(engine.config, engine.player.x, engine.player.y, minimapScale, player.spellBook.sustainedActive);

							// Draw direction arrow
							const mctx = canvas.getContext('2d');
							if (mctx) {
								const mw = canvas.width;
								const mh = canvas.height;
								const cx = mw / 2;
								const cy = mh / 2;
								const arrowLen = Math.min(mw, mh) * 0.18;
								// Camera yaw: 0 = +X, π/2 = +Y → canvas: right = +X, down = +Y
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
								// Arrowhead
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
					} else if (renderer) {
						const effectiveScale = (engine.config.scale || 40) * zoom;
						renderer.render(engine.config, engine.player.x, engine.player.y, effectiveScale, player.spellBook.sustainedActive);

						// Draw targeting line from player to cursor
						if (activeSpell && canvas) {
							const ctx = canvas.getContext('2d');
							if (ctx) {
								const w = canvas.width;
								const h = canvas.height;
								const ox = w / 2;
								const oy = h / 2;
								const tx = ox + (mouseWorldX - engine.player.x) * effectiveScale;
								const ty = oy + (mouseWorldY - engine.player.y) * effectiveScale;
								ctx.save();
								ctx.strokeStyle = 'rgba(255, 255, 200, 0.35)';
								ctx.lineWidth = 1;
								ctx.setLineDash([6, 4]);
								ctx.beginPath();
								ctx.moveTo(ox, oy);
								ctx.lineTo(tx, ty);
								ctx.stroke();
								ctx.restore();
							}
						}
					}
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

			exitSubworld();
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

		// Escape pointer lock in 3D mode
		if (view3d && event.key === 'Escape' && document.pointerLockElement === canvas3d) {
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

	function handleWheel(event: WheelEvent) {
		event.preventDefault();
		zoom = event.deltaY < 0 ? Math.min(ZOOM_MAX, zoom * ZOOM_STEP) : Math.max(ZOOM_MIN, zoom / ZOOM_STEP);
	}

	function handleMouseMove(event: MouseEvent) {
		if (!engine) {
			return;
		}

		// 3D pointer lock: mouse movement rotates camera
		if (view3d && camera && document.pointerLockElement === canvas3d) {
			rotateCamera(camera, event.movementX, event.movementY);
			return;
		}

		if (!canvas) {
			return;
		}

		const rect = canvas.getBoundingClientRect();
		const dpr = window.devicePixelRatio || 1;
		const scale = (engine.config.scale || 40) * zoom;
		const cx = (event.clientX - rect.left) * dpr;
		const cy = (event.clientY - rect.top) * dpr;
		const w = canvas.width;
		const h = canvas.height;
		const ox = w / 2 - engine.player.x * scale;
		const oy = h / 2 - engine.player.y * scale;
		mouseWorldX = (cx - ox) / scale;
		mouseWorldY = (cy - oy) / scale;
	}

	function handleCanvasClick() {
		if (view3d && canvas3d && document.pointerLockElement !== canvas3d) {
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
		const cast = engine.castSpell(dmg, projSpeed, projRadius, blast, activeSpell.micro?.friendlyFire ?? false, mouseWorldX, mouseWorldY, spellColor, activeSpell.id);
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
			<span class="font-bold uppercase tracking-wider" style="color: {color.accent};">{locationName}</span>
			<span style="color: {color.hp};">HP: {player.combatStats.currentHp}/{player.combatStats.maxHp}</span>
			<span style="color: {color.mp};">MP: {Math.floor(player.combatStats.currentMp)}/{player.combatStats.maxMp}</span>
			<span style="color: {color.sp};">SP: {Math.floor(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
			<span class="text-yellow-400">Gold: {player.gold}</span>
			{#if friendlyCount > 0}
				<span class="text-green-400">Allies: {friendlyCount}</span>
			{/if}
			{#if enemyCount > 0}
				<span class="text-red-400">Enemies: {enemyCount}</span>
			{/if}
		</div>
		<button onclick={exitSubworld}
			class="rounded border-2 px-3 py-1 text-xs font-bold uppercase tracking-wide transition"
			{...btnProps('close')}
		>Leave [Esc]</button>
		<button onclick={() => {
			view3d = !view3d;
		}}
			class="rounded border-2 px-3 py-1 text-xs font-bold uppercase tracking-wide transition"
			{...btnProps('action')}
		>{view3d ? '2D' : '3D'}</button>
	</div>

	<!-- Canvas -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div class="relative flex-1" onwheel={handleWheel} onmousemove={handleMouseMove} onclick={handleCanvasClick}>
		<canvas bind:this={canvas3d} class="h-full w-full" class:hidden={!view3d}></canvas>
		<!-- 2D canvas: full when 2D mode; circular minimap overlay when 3D mode -->
		<canvas
			bind:this={canvas}
			class={view3d
				? 'pointer-events-none absolute right-4 top-4 h-64 w-64 rounded-full border-2 border-white/30 shadow-lg'
				: 'h-full w-full'}
			style="image-rendering: pixelated;{view3d ? ' object-fit: cover;' : ''}"
		></canvas>

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
			{#if view3d}
				Click to look · Arrows to move · A to attack · S to cast spell · Space to pause
			{:else}
				Arrows to move · A to attack · S to cast spell · Space to pause
			{/if}
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
			<div class="absolute bottom-4 left-1/2 -translate-x-1/2 rounded border px-4 py-2 text-center font-sans text-sm shadow-lg" style={messageStyle}>
				{message}
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
				zoom,
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
					view3d,
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
</div>

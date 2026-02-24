<script lang="ts">
	import {onMount} from 'svelte';
	import {type GameState, type Settlement, createGameState, saveGame} from '../game/state';
	import {MapGenerator} from '../webgl/map-generator';
	import {GameRenderer, type EntityData, SPRITE_TREE} from '../game/renderer';
	import {findPath} from '../game/pathfinding';
	import {
		type NPC,
		NPCType,
		spawnNPCs,
		tickNPCs,
		SPRITE_CITY,
		spawnCityNPCs,
		tickCityNPCs,
	} from '../game/npc';
	import {
		type AnimationState, type Direction, type CharacterData,
		loadAtlas, AnimationManager, LOGICAL_TILE_SIZE,
	} from '../character';
	import {CityGenerator, type CityMapData} from '../game/city-generator';
	import {type TraversabilityData} from '../webgl/map-generator';
	import PauseOverlay from './PauseOverlay.svelte';
	import StatOverlay from './StatOverlay.svelte';
	import SettlementOverlay from './SettlementOverlay.svelte';
	import EventOverlay from './EventOverlay.svelte';
	import BattleOverlay from './BattleOverlay.svelte';
	import InteractionOverlay from './InteractionOverlay.svelte';
	import TradeOverlay from './TradeOverlay.svelte';
	import MapOverlay from './MapOverlay.svelte';
	import DebugOverlay from './DebugOverlay.svelte';
	import CodexOverlay from './CodexOverlay.svelte';
	import {type RandomEvent, rollForEvent} from '../game/events';
	import type {Inventory} from '../game/items';
	import {loadTrack, playTrack} from '../game/audio';

	type Props = {
		gameState: GameState;
		onBackToTitle: () => void;
		onLoadGame: (key: string) => void;
	};

	let {gameState, onBackToTitle, onLoadGame}: Props = $props();

	let gState: GameState = $state(JSON.parse(JSON.stringify(gameState)) as GameState);
	let visualPlayerX = $state(gState.player.x);
	let visualPlayerY = $state(gState.player.y);
	let paused = $state(false);
	let showCodex = $state(false);
	let showStat = $state(false);
	let showInventory = $state(false);
	let showSettlement = $state(false);
	let showMap = $state(false);
	let showDebug = $state(false);
	let debugFps = $state(0);
	let debugFrameDt = $state(0);
	let canvas: HTMLCanvasElement;
	let mapGenerator: MapGenerator | undefined;
	let gameRenderer: GameRenderer | undefined;
	let animFrameId = 0;
	let movePath: Array<{x: number; y: number}> = $state([]);
	let moveIndex = $state(0);
	let simSpeed = $state(1); // 0=paused, 1=normal, 2=fast
	let hoverTileX = -1;
	let hoverTileY = -1;
	let hoveredNpc: NPC | undefined = $state(undefined);
	let activeEvent: RandomEvent | undefined = $state(undefined);
	let battleInfo: {enemyName: string; enemyType: NPCType; enemyLevel: number} | undefined = $state(undefined);
	let interactingNpc: NPC | undefined = $state(undefined);
	let tradeNpc: {npc: NPC; inventory: Inventory} | undefined = $state(undefined);
	let tradeSettlement: {settlement: Settlement} | undefined = $state(undefined);
	
	// City State
	let inCity = $state(false);
	let cityData: CityMapData | undefined = undefined;
	let cityTexture: WebGLTexture | undefined = undefined;
	let cityTraversability: TraversabilityData | undefined = undefined;
	let overworldPlayerX = 0;
	let overworldPlayerY = 0;

	let stepsSincLastEvent = 0;
	let npcs: NPC[] = [];
	let cityNpcs: NPC[] = $state([]); // Текущие жители города
	let trees: Array<{x: number; y: number}> = [];
	let mapW = 1024;
	let mapH = 1024;
	let npcTickTimer = 0;
	let _charRenderDiagDone = false;
	let worldTimeAccumulator = 0;

	// Character animation state (not serialized)
	let playerAnimState: AnimationState = AnimationManager.createAnimationState();
	const npcAnimStates = new Map<number, AnimationState>();
	const NPC_TICK_INTERVAL = 500; // ms between NPC movement ticks

	// Continuous keyboard movement state
	const pressedKeys = new Set<string>();
	let isShiftHeld = false;
	const WALK_SPEED = 3; // tiles per second
	const RUN_SPEED = 6; // tiles per second (shift held)
	const MS_PER_GAME_MINUTE = 1000; // 1 real second = 1 game minute at speed 1

	// Pan / zoom state
	let isDragging = false;
	let dragStartX = 0;
	let dragStartY = 0;
	let dragCamStartX = 0;
	let dragCamStartY = 0;
	let panVelocityX = 0;
	let panVelocityY = 0;
	let lastPointerX = 0;
	let lastPointerY = 0;
	let lastPointerTime = 0;
	let pinchStartDist = 0;
	let pinchStartZoom = 0;
	let dragDistance = 0;
	const MIN_ZOOM = 8;
	const MAX_ZOOM = 200;
	const PAN_FRICTION = 0.92;
	const PAN_MIN_VELOCITY = 0.00001;

	// HUD derived values (pure — no mutations allowed)
	let currentSettlement = $derived.by(() => {
		if (gState.settlements.length === 0) {
			return undefined;
		}

		return gState.settlements.find(
			s => Math.abs(s.x - gState.player.x) < 3 && Math.abs(s.y - gState.player.y) < 3,
		);
	});

	let currentSettlementName = $derived(currentSettlement?.name ?? '');

	function syncCurrentSettlement() {
		const found = gState.settlements.find(
			s => Math.abs(s.x - gState.player.x) < 3 && Math.abs(s.y - gState.player.y) < 3,
		);
		gState.player.currentSettlement = found?.name;
	}

	let timeString = $derived(
		`${String(gState.worldTime.hour).padStart(2, '0')}:${String(gState.worldTime.minute).padStart(2, '0')}`,
	);

	onMount(() => {
		initGame();
		
		// Load audio tracks
		void loadTrack('explore', '/assets/sound/15-dungeon-suno.mp3');
		void loadTrack('battle', '/assets/sound/empire-theme.mp3');

		// Add touch listeners with {passive:false} so preventDefault works for pinch zoom
		canvas.addEventListener('touchstart', handleTouchStart, {passive: false});
		canvas.addEventListener('touchmove', handleTouchMove, {passive: false});

		return () => {
			cancelAnimationFrame(animFrameId);
			canvas.removeEventListener('touchstart', handleTouchStart);
			canvas.removeEventListener('touchmove', handleTouchMove);
			gameRenderer?.destroy();
			mapGenerator?.destroy();
		};
	});

	async function initGame() {
		// MapGenerator creates its own GL context from the canvas
		mapGenerator = new MapGenerator(canvas, gState.mapParams);
		mapGenerator.generateAll();

		// If no settlements yet (New Game flow), populate them
		const dims = mapGenerator.getMapDimensions();
		mapW = dims.width;
		mapH = dims.height;

		if (gState.settlements.length === 0) {
			const cities = mapGenerator.getCities();
			const populated = createGameState(gState.mapParams, cities, mapW, mapH);
			gState.settlements = populated.settlements;
			gState.player = populated.player;
		}

		// Share the same GL context so GameRenderer can access map textures
		const gl = mapGenerator.getGL();
		gameRenderer = new GameRenderer(gl, mapW, mapH);
		gameRenderer.setZoom(40);
		gameRenderer.setCamera(
			gState.player.x / mapW,
			gState.player.y / mapH,
		);
		gameRenderer.setWorldSeed(gState.seed);

		// Load sprite atlas, terrain textures, and character atlas
		await gameRenderer.loadSprites();
		await gameRenderer.loadTerrainTextures();
		await loadAtlas();
		gameRenderer.initCharacterRenderer();

		// Scatter trees on traversable land (not near settlements/roads)
		trees = spawnTrees(gState.seed);

		// Spawn NPCs (after trees, so isLand checker is available)
		npcs = spawnNPCs(
			gState.settlements, gState.seed, mapW, mapH,
			(x, y) => mapGenerator?.isTraversable(x, y) ?? false,
		);

		uploadEntityData();

		startLoop();
	}

	function startLoop() {
		let lastTime = performance.now();
		let fpsAccum = 0;
		let fpsFrames = 0;

		function frame(now: number) {
			const dt = now - lastTime;
			lastTime = now;

			// FPS tracking for debug overlay
			fpsAccum += dt;
			fpsFrames++;
			if (fpsAccum >= 500) {
				debugFps = (fpsFrames / fpsAccum) * 1000;
				debugFrameDt = fpsAccum / fpsFrames;
				fpsAccum = 0;
				fpsFrames = 0;
			}

			if (!paused && simSpeed > 0) {
				const scaledDt = dt * simSpeed;
				updateKeyboardMovement(dt);
				updateMovement(scaledDt);
				updateNPCs(scaledDt);
				updateWorldTime(scaledDt);
				updateNightDarken();
				interpolateVisualPositions(dt, simSpeed);
				updateCharacterAnimations(dt);
			}

			updatePanInertia();
			renderFrame();
			animFrameId = requestAnimationFrame(frame);
		}

		animFrameId = requestAnimationFrame(frame);
	}

	function getInputVector(): {x: number; y: number} {
		return {
			x: (pressedKeys.has('ArrowRight') || pressedKeys.has('d') ? 1 : 0)
				- (pressedKeys.has('ArrowLeft') || pressedKeys.has('a') ? 1 : 0),
			// ArrowUp = +Y (upward in GL map coords)
			y: (pressedKeys.has('ArrowUp') || pressedKeys.has('w') ? 1 : 0)
				- (pressedKeys.has('ArrowDown') || pressedKeys.has('s') ? 1 : 0),
		};
	}

	function tryStepPlayer(dx: number, dy: number): boolean {
		const currentMapW = inCity && cityData ? cityData.width : mapW;
		const currentMapH = inCity && cityData ? cityData.height : mapH;

		let nx = gState.player.x + dx;
		let ny = gState.player.y + dy;

		if (inCity) {
			if (nx < 0 || nx >= currentMapW || ny < 0 || ny >= currentMapH) {
				leaveCity();
				return false;
			}
		} else {
			nx = (nx % mapW + mapW) % mapW;
			ny = (ny % mapH + mapH) % mapH;
		}

		const isWalkable = inCity
			? (cityTraversability?.data[ny * currentMapW + nx] ?? 0) > 127
			: (mapGenerator?.isTraversable(nx, ny) ?? false);

		if (!isWalkable) {
			return false;
		}

		gState.player.x = nx;
		gState.player.y = ny;
		stepsSincLastEvent++;

		if (!inCity) {
			syncCurrentSettlement();
		}

		const evt = rollForEvent(stepsSincLastEvent);
		if (evt) {
			activeEvent = evt;
			stepsSincLastEvent = 0;
		}

		return true;
	}

	function updateKeyboardMovement(_dt: number) {
		// Block movement when overlays are active
		if (activeEvent || battleInfo || interactingNpc || tradeNpc || tradeSettlement || showStat || showInventory || showSettlement) {
			return;
		}

		const vec = getInputVector();
		if (vec.x === 0 && vec.y === 0) {
			return;
		}

		// Cancel any click-to-move path when using keyboard
		if (movePath.length > 0) {
			movePath = [];
			moveIndex = 0;
		}

		// Only advance logical position when visual has arrived (or nearly so)
		const distToLogical = Math.abs(gState.player.x - visualPlayerX)
			+ Math.abs(gState.player.y - visualPlayerY);
		// Diagonal steps cover sqrt(2) ≈ 1.41 tiles, so use a larger threshold
		if (distToLogical > 0.2) {
			return; // Still walking to current logical tile — wait
		}

		// Allow diagonal: clamp each axis to -1/0/+1
		const dx = Math.sign(vec.x);
		const dy = Math.sign(vec.y);

		// Try diagonal first; if blocked, fall back to cardinal axes
		if (dx !== 0 && dy !== 0) {
			if (!tryStepPlayer(dx, dy)) {
				// Diagonal blocked — try each axis individually
				if (!tryStepPlayer(dx, 0)) {
					tryStepPlayer(0, dy);
				}
			}
		} else {
			tryStepPlayer(dx, dy);
		}
	}

	function updateMovement(_dt: number) {
		if (movePath.length === 0 || moveIndex >= movePath.length) {
			return;
		}

		// Advance logical position when visual has nearly arrived
		const distToLogical = Math.hypot(
			gState.player.x - visualPlayerX,
			gState.player.y - visualPlayerY,
		);
		if (distToLogical > 0.2) {
			return; // Still walking to current tile
		}

		const next = movePath[moveIndex];
		gState.player.x = next.x;
		gState.player.y = next.y;
		moveIndex++;
		stepsSincLastEvent++;

		// Roll for random event
		const evt = rollForEvent(stepsSincLastEvent);
		if (evt) {
			activeEvent = evt;
			stepsSincLastEvent = 0;
			movePath = [];
			moveIndex = 0;
			return;
		}

		if (moveIndex >= movePath.length) {
			movePath = [];
			moveIndex = 0;
		}

		if (!inCity) {
			syncCurrentSettlement();
		}
	}

	function updateNPCs(dt: number) {
		npcTickTimer += dt;
		if (npcTickTimer >= NPC_TICK_INTERVAL) {
			npcTickTimer -= NPC_TICK_INTERVAL;
			
			if (inCity && cityData) {
				// Прямой вызов оптимизированной функции
				tickCityNPCs(cityNpcs, cityData.grid, cityData.width, cityData.height);
			} else {
				// Глобальные NPC
				tickNPCs(npcs, {
					mapWidth: mapW,
					mapHeight: mapH,
					isTraversable: (x, y) => mapGenerator?.isTraversable(x, y) ?? false,
					settlements: gState.settlements,
					trees,
					playerX: gState.player.x,
					playerY: gState.player.y,
				});
			}
			uploadEntityData();
		}
	}

	function uploadEntityData() {
		if (!gameRenderer) return;

		const entities: EntityData[] = [];

		// Отрисовываем глобальные объекты (города и деревья) ТОЛЬКО если мы не в подмире
		if (!inCity) {
			for (const settlement of gState.settlements) {
				entities.push({
					x: settlement.x, y: settlement.y,
					type: SPRITE_CITY, active: true, scale: 1.8,
				});
			}

			for (const tree of trees) {
				entities.push({
					x: tree.x, y: tree.y,
					type: SPRITE_TREE, active: true, scale: 1.0,
				});
			}
		}

		// NPCs and player are now rendered via CharacterRenderer (post-pass)

		gameRenderer.uploadEntities(entities);
	}

	function spawnTrees(seed: number): Array<{x: number; y: number}> {
		if (!mapGenerator) {
			return [];
		}

		const tData = mapGenerator.getTraversabilityData();
		if (!tData) {
			return [];
		}

		let s = seed + 3333;
		const rng = (): number => {
			s = (s * 16_807 + 0) % 2_147_483_647;
			return s / 2_147_483_647;
		};

		// Hash-based noise for forest zone shaping
		const noise = (x: number, y: number, sd: number): number => {
			const n = Math.sin(x * 12.9898 + y * 78.233 + sd * 43758.5453) * 43758.5453;
			return n - Math.floor(n);
		};

		const fbm = (x: number, y: number, sd: number, octaves: number): number => {
			let value = 0;
			let amp = 1;
			let maxAmp = 0;
			let freq = 1;
			for (let i = 0; i < octaves; i++) {
				value += noise(x * freq, y * freq, sd + i * 100) * amp;
				maxAmp += amp;
				amp *= 0.5;
				freq *= 2;
			}

			return value / maxAmp;
		};

		const MAX_TREES = 4000;
		const mw = tData.width;
		const mh = tData.height;
		const STEP = 3;

		// Phase 1: Collect ALL valid candidate positions with their density weight
		const candidates: Array<{x: number; y: number; w: number}> = [];

		for (let y = 0; y < mh; y += STEP) {
			for (let x = 0; x < mw; x += STEP) {
				const idx = y * mw + x;

				// Must be traversable land
				if (tData.data[idx] < 127) continue;

				// Skip ice — trees cannot grow on frozen water
				if (tData.iceData[idx] > 0) continue;

				// Skip roads
				if (tData.roadData[idx] > 25) continue;

				// Height filter: no water (below sea level), no snow peaks (high)
				const h = tData.heightData[idx] / 255;
				if (h - 0.05 < gState.mapParams.seaLevel || h > 0.65) continue;

				// Skip near settlements
				let nearSettlement = false;
				for (const st of gState.settlements) {
					if (Math.abs(st.x - x) < 12 && Math.abs(st.y - y) < 12) {
						nearSettlement = true;
						break;
					}
				}

				if (nearSettlement) continue;

				// Forest density from noise — creates clustered zones
				const nx = x / mw;
				const ny = y / mh;
				const forestNoise = fbm(nx * 6, ny * 6, seed + 500, 4);
				const clusterNoise = fbm(nx * 24, ny * 24, seed + 700, 3);
				const heightFactor = 1 - Math.abs(h - 0.35) * 2.5;
				const density = forestNoise * 0.55 + clusterNoise * 0.3 + Math.max(0, heightFactor) * 0.15;

				// Only keep positions in forest zones (density > threshold)
				if (density > 0.44) {
					// Small random jitter for natural look
					const ox = Math.floor(rng() * STEP);
					const oy = Math.floor(rng() * STEP);
					candidates.push({
						x: (x + ox) % mw,
						y: (y + oy) % mh,
						w: density,
					});
				}
			}
		}

		// Phase 2: Fisher-Yates shuffle weighted toward high-density spots
		// Sort by density descending so forests are dense in cluster centers
		candidates.sort((a, b) => b.w - a.w);

		// Take the densest candidates up to MAX_TREES
		const result = candidates.slice(0, MAX_TREES).map(c => ({x: c.x, y: c.y}));

		return result;
	}

	function advanceWorldMinute() {
		gState.worldTime.minute += 1;
		if (gState.worldTime.minute >= 60) {
			gState.worldTime.minute = 0;
			gState.worldTime.hour += 1;
			if (gState.worldTime.hour >= 24) {
				gState.worldTime.hour = 0;
				gState.worldTime.day += 1;
			}
		}
	}

	function updateWorldTime(dt: number) {
		worldTimeAccumulator += dt;
		while (worldTimeAccumulator >= MS_PER_GAME_MINUTE) {
			worldTimeAccumulator -= MS_PER_GAME_MINUTE;
			advanceWorldMinute();
		}
	}

	function updateNightDarken() {
		if (!gameRenderer) {
			return;
		}

		const {hour, minute} = gState.worldTime;
		const progress = (hour + minute / 60) / 24;

		// Port of old concept get_ambient_color:
		// 0.0-0.2 (0:00-4:48): full dark
		// 0.2-0.35 (4:48-8:24): dawn
		// 0.35-0.75 (8:24-18:00): full daylight
		// 0.75-0.9 (18:00-21:36): dusk
		// 0.9-1.0 (21:36-24:00): full dark
		let darken = 0;
		if (progress < 0.2 || progress > 0.9) {
			darken = 1;
		} else if (progress < 0.35) {
			darken = 1 - (progress - 0.2) / 0.15;
		} else if (progress < 0.75) {
			darken = 0;
		} else {
			darken = (progress - 0.75) / 0.15;
		}

		gameRenderer.setNightDarken(darken);
	}

	function handleEventClose() {
		activeEvent = undefined;
	}

	function handleEventBattle(enemyName: string, enemyType: number, enemyLevel: number) {
		activeEvent = undefined;
		battleInfo = {enemyName, enemyType: enemyType as NPCType, enemyLevel};
		void playTrack('battle');
	}

	function handleBattleEnd(_victory: boolean) {
		battleInfo = undefined;
		void playTrack('explore');
	}

	function handleInteractionClose() {
		interactingNpc = undefined;
	}

	function handleInteractionFight() {
		const npc = interactingNpc;
		if (!npc) {
			return;
		}

		interactingNpc = undefined;
		battleInfo = {
			enemyName: npc.name,
			enemyType: npc.type,
			enemyLevel: npc.level,
		};
		void playTrack('battle');
	}

	function handleInteractionTrade() {
		const npc = interactingNpc;
		if (!npc) {
			return;
		}

		interactingNpc = undefined;
		// Use NPC's own inventory (universal system)
		tradeNpc = {npc, inventory: npc.inventory};
	}

	function handleTradeClose() {
		tradeNpc = undefined;
		tradeSettlement = undefined;
	}

	function handleSettlementTrade() {
		if (!currentSettlement) {
			return;
		}

		showSettlement = false;
		tradeSettlement = {settlement: currentSettlement};
	}

	// Move (vx,vy) toward (tx,ty) by at most maxStep distance (Euclidean).
	// Returns [newX, newY].
	function moveToward2D(vx: number, vy: number, tx: number, ty: number, maxStep: number): [number, number] {
		if (!(maxStep > 0)) {
			return [tx, ty];
		}

		const dx = tx - vx;
		const dy = ty - vy;
		const dist = Math.hypot(dx, dy);
		if (dist <= maxStep) {
			return [tx, ty];
		}

		const ratio = maxStep / dist;
		return [vx + dx * ratio, vy + dy * ratio];
	}

	function interpolateVisualPositions(dt: number, speed: number) {
		const dtSec = dt / 1000;

		// Player: constant-speed linear movement toward logical tile
		// Shift inverted: run by default, walk when shift held
		const playerSpeed = isShiftHeld ? WALK_SPEED : RUN_SPEED;
		const playerStep = playerSpeed * dtSec;
		const pdx = gState.player.x - visualPlayerX;
		const pdy = gState.player.y - visualPlayerY;
		if (Math.abs(pdx) > 3 || Math.abs(pdy) > 3) {
			visualPlayerX = gState.player.x;
			visualPlayerY = gState.player.y;
		} else {
			[visualPlayerX, visualPlayerY] = moveToward2D(
				visualPlayerX, visualPlayerY,
				gState.player.x, gState.player.y,
				playerStep,
			);
		}

		// Camera follows visual player
		if (gameRenderer) {
			const currentMapW = inCity && cityData ? cityData.width : mapW;
			const currentMapH = inCity && cityData ? cityData.height : mapH;
			gameRenderer.setCamera(
				visualPlayerX / currentMapW,
				visualPlayerY / currentMapH,
			);
		}

		// NPCs: per-NPC speed set at tick time (dist/tickSec), scaled by simSpeed
		const activeNpcList = inCity ? cityNpcs : npcs;
		for (const npc of activeNpcList) {
			// Guard: snap to logical position if visual coords are invalid
			if (Number.isNaN(npc.visualX) || Number.isNaN(npc.visualY)) {
				npc.visualX = npc.x;
				npc.visualY = npc.y;
			}

			const ndx = npc.x - npc.visualX;
			const ndy = npc.y - npc.visualY;
			if (Math.abs(ndx) > 3 || Math.abs(ndy) > 3) {
				npc.visualX = npc.x;
				npc.visualY = npc.y;
			} else {
				const s = (npc.visualSpeed || 2) * speed * dtSec;
				[npc.visualX, npc.visualY] = moveToward2D(
					npc.visualX, npc.visualY,
					npc.x, npc.y,
					s,
				);
			}
		}
	}

	function resolveDirection(dx: number, dy: number): Direction {
		if (Math.abs(dx) > Math.abs(dy)) {
			return dx > 0 ? 'right' : 'left';
		}

		// Map Y increases upward (GL convention), but 'front' = facing camera (south)
		return dy > 0 ? 'back' : 'front';
	}

	function updateCharacterAnimations(dt: number) {
		// Player animation: detect movement from visual→logical delta
		const pdx = gState.player.x - visualPlayerX;
		const pdy = gState.player.y - visualPlayerY;
		const playerDist = Math.hypot(pdx, pdy);
		const playerMoving = playerDist > 0.05;

		if (playerMoving) {
			const dir = resolveDirection(pdx, pdy);
			// Shift inverted: run by default, walk when shift held
			const moveAnim = isShiftHeld ? 'walk' : 'run';
			playerAnimState = AnimationManager.setAnimation(playerAnimState, moveAnim);
			playerAnimState = AnimationManager.setDirection(playerAnimState, dir);
		} else {
			playerAnimState = AnimationManager.setAnimation(playerAnimState, 'idle');
		}

		playerAnimState = AnimationManager.updateAnimation(playerAnimState, dt);

		// NPC animations
		const activeNpcList = inCity ? cityNpcs : npcs;
		for (const npc of activeNpcList) {
			if (npc.hp <= 0) {
				continue;
			}

			let animState = npcAnimStates.get(npc.id);
			if (!animState) {
				animState = AnimationManager.createAnimationState();
				npcAnimStates.set(npc.id, animState);
			}

			// Detect movement from visual→logical delta (smooth interpolation)
			const ndx = npc.x - npc.visualX;
			const ndy = npc.y - npc.visualY;
			const npcDist = Math.hypot(ndx, ndy);
			// Hysteresis: start walking at 0.05, only stop at 0.01
			// This prevents walk/idle flicker at tile boundaries
			const wasMoving = animState.currentAnimation === 'walk';
			const isMoving = wasMoving ? npcDist > 0.01 : npcDist > 0.05;

			if (isMoving) {
				animState = AnimationManager.setAnimation(animState, 'walk');
				const dir = resolveDirection(ndx, ndy);
				animState = AnimationManager.setDirection(animState, dir);
			} else {
				animState = AnimationManager.setAnimation(animState, 'idle');
			}

			animState = AnimationManager.updateAnimation(animState, dt);
			npcAnimStates.set(npc.id, animState);
		}
	}

	function renderCharacters(canvasW: number, canvasH: number) {
		if (!gameRenderer) {
			return;
		}

		const charRenderer = gameRenderer.getCharacterRenderer();
		if (!charRenderer) {
			return;
		}

		// Collect all characters with screen positions, sorted by Y for depth
		type CharEntry = {
			screenX: number;
			screenY: number;
			data: CharacterData;
			anim: AnimationState;
		};
		const entries: CharEntry[] = [];

		// Compute character scale: how many pixels per tile on screen
		const tilesVisible = gameRenderer.getZoom();
		const tilePixels = canvasW / tilesVisible;
		const charScale = tilePixels / LOGICAL_TILE_SIZE;

		// NPCs
		const activeNpcList = inCity ? cityNpcs : npcs;
		let _diagNpcTotal = 0;
		let _diagNpcCulled = 0;
		let _diagNpcNaN = 0;
		for (const npc of activeNpcList) {
			if (npc.hp <= 0) {
				continue;
			}

			_diagNpcTotal++;

			const screen = gameRenderer.worldToScreen(npc.visualX, npc.visualY, canvasW, canvasH);
			if (!screen) {
				_diagNpcCulled++;
				continue;
			}

			const sx = screen.sx - (LOGICAL_TILE_SIZE * charScale) / 2;
			const sy = screen.sy - (LOGICAL_TILE_SIZE * charScale) / 2;
			if (Number.isNaN(sx) || Number.isNaN(sy)) {
				_diagNpcNaN++;
			}

			const animState = npcAnimStates.get(npc.id) ?? AnimationManager.createAnimationState();
			entries.push({
				screenX: sx,
				screenY: sy,
				data: npc.characterData,
				anim: animState,
			});
		}

		if (!_charRenderDiagDone && activeNpcList.length > 0) {
			_charRenderDiagDone = true;
			const firstNpc = activeNpcList.find(n => n.hp > 0);
			console.log('[renderCharacters] DIAG', {
				npcListLen: activeNpcList.length,
				alive: _diagNpcTotal,
				culled: _diagNpcCulled,
				nanCount: _diagNpcNaN,
				entries: entries.length,
				canvasW, canvasH,
				charScale: charScale.toFixed(4),
				tilesVisible,
				firstNpc: firstNpc ? {
					x: firstNpc.x, y: firstNpc.y,
					vx: firstNpc.visualX, vy: firstNpc.visualY,
					vxNaN: Number.isNaN(firstNpc.visualX),
					vyNaN: Number.isNaN(firstNpc.visualY),
				} : null,
				playerVx: visualPlayerX, playerVy: visualPlayerY,
			});
		}

		// Player (always on top via Y sort — player drawn after NPCs at same Y)
		const playerScreen = gameRenderer.worldToScreen(visualPlayerX, visualPlayerY, canvasW, canvasH);
		if (playerScreen) {
			entries.push({
				screenX: playerScreen.sx - (LOGICAL_TILE_SIZE * charScale) / 2,
				screenY: playerScreen.sy - (LOGICAL_TILE_SIZE * charScale) / 2,
				data: gState.player.characterData,
				anim: playerAnimState,
			});
		}

		// Sort by Y for depth ordering
		entries.sort((a, b) => a.screenY - b.screenY);

		for (const entry of entries) {
			charRenderer.drawCharacter(entry.data, entry.anim, entry.screenX, entry.screenY, charScale, canvasW, canvasH);
		}
	}

	function renderFrame() {
		if (!gameRenderer || !mapGenerator || !canvas) {
			return;
		}

		const dpr = window.devicePixelRatio || 1;
		const w = Math.round(canvas.clientWidth * dpr);
		const h = Math.round(canvas.clientHeight * dpr);
		if (canvas.width !== w || canvas.height !== h) {
			canvas.width = w;
			canvas.height = h;
		}

		let texture_to_render: WebGLTexture | null | undefined;
		
		if (inCity && cityTexture) {
			texture_to_render = cityTexture;
		} else {
			texture_to_render = mapGenerator.getVisualTexture();
		}

		if (!texture_to_render) {
			return;
		}

		uploadEntityData();
		gameRenderer.render(texture_to_render, visualPlayerX, visualPlayerY, w, h, hoverTileX, hoverTileY);

		// Character post-pass: render after map + instanced sprites
		renderCharacters(w, h);
	}

	function handleCanvasClick(event: MouseEvent) {
		if (paused || activeEvent || battleInfo || interactingNpc || tradeNpc || tradeSettlement || showStat || showInventory || showSettlement || !gameRenderer || !mapGenerator) {
			return;
		}

		const rect = canvas.getBoundingClientRect();
		const screenX = (event.clientX - rect.left) * (canvas.width / rect.width);
		const screenY = (event.clientY - rect.top) * (canvas.height / rect.height);

		const currentMapW = inCity && cityData ? cityData.width : mapW;
		const currentMapH = inCity && cityData ? cityData.height : mapH;		
		const target = gameRenderer.screenToTile(screenX, screenY, canvas.width, canvas.height);
		
		// Выбираем правильный список NPC в зависимости от контекста
		const activeNpcList = inCity ? cityNpcs : npcs;
		const clickedNpc = activeNpcList.find(
			n => n.hp > 0 && Math.abs(n.x - target.x) < 2 && Math.abs(n.y - target.y) < 2,
		);
		if (clickedNpc) {
			interactingNpc = clickedNpc;
			return;
		}

		let traversabilityData: TraversabilityData | null | undefined;
		
		if (inCity) {
			traversabilityData = cityTraversability;
		} else {
			traversabilityData = mapGenerator.getTraversabilityData();
		}

		if (!traversabilityData) {
			return;
		}

		const result = findPath(
			traversabilityData,
			gState.player.x,
			gState.player.y,
			target.x,
			target.y,
		);

		if (result.found && result.path.length > 1) {
			movePath = result.path.slice(1); // Skip current position
			moveIndex = 0;
		}
	}

	const moveKeys = new Set([
		'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
		'w', 'a', 's', 'd',
	]);

	function handleKeyDown(event: KeyboardEvent) {
		// Shift tracking for run
		if (event.key === 'Shift') {
			isShiftHeld = true;
			return;
		}

		// Debug overlay toggle
		if (event.key === '`') {
			showDebug = !showDebug;
			return;
		}

		if (event.key === 'Escape') {
			if (showDebug) {
				showDebug = false;
			} else if (showCodex) {
				showCodex = false;
			} else if (showStat) {
				showStat = false;
			} else {
				paused = !paused;
			}

			return;
		}

		if (event.key === 'c' || event.key === 'C') {
			if (!paused && !showSettlement && !showInventory) {
				showStat = !showStat;
			}

			return;
		}

		if (event.key === 'i' || event.key === 'I') {
			if (!paused && !showSettlement && !showStat) {
				showInventory = !showInventory;
			}

			return;
		}

		if (event.key === 'm' || event.key === 'M') {
			if (!paused && !showStat && !showInventory && !showSettlement) {
				showMap = !showMap;
			}

			return;
		}

		if (event.key === 'e' || event.key === 'E') {
			if (!paused && !showStat && !showInventory) {
				if (inCity) {
					leaveCity();
				} else if (currentSettlementName) {
					showSettlement = true;
				} else {
					enterSubworld('nature');
				}
			}

			return;
		}

		// Track movement keys — actual movement handled per-frame
		if (moveKeys.has(event.key)) {
			event.preventDefault();
			pressedKeys.add(event.key);
		}
	}

	function handleKeyUp(event: KeyboardEvent) {
		if (event.key === 'Shift') {
			isShiftHeld = false;
		}

		pressedKeys.delete(event.key);
	}

	function handleCanvasHover(event: MouseEvent) {
		if (!gameRenderer || !canvas) {
			return;
		}

		const rect = canvas.getBoundingClientRect();
		const screenX = (event.clientX - rect.left) * (canvas.width / rect.width);
		const screenY = (event.clientY - rect.top) * (canvas.height / rect.height);
		const tile = gameRenderer.screenToTile(screenX, screenY, canvas.width, canvas.height);
		hoverTileX = tile.x;
		hoverTileY = tile.y;

		// Выбираем правильный список NPC в зависимости от контекста
		const activeNpcList = inCity ? cityNpcs : npcs;
		hoveredNpc = activeNpcList.find(
			n => n.hp > 0 && Math.abs(n.x - tile.x) < 2 && Math.abs(n.y - tile.y) < 2,
		);
	}

	function handleSave() {
		gState.subState = {type: 'exploring'};
		saveGame(gState);
	}

	function handleResume() {
		paused = false;
	}

	function handlePauseLoad(key: string) {
		paused = false;
		onLoadGame(key);
	}

	function handleToTitle() {
		paused = false;
		onBackToTitle();
	}

	function zoomIn() {
		if (gameRenderer) {
			gameRenderer.setZoom(Math.max(MIN_ZOOM, gameRenderer.getZoom() - 5));
		}
	}

	function zoomOut() {
		if (gameRenderer) {
			gameRenderer.setZoom(Math.min(MAX_ZOOM, gameRenderer.getZoom() + 5));
		}
	}

	function applyZoom(newZoom: number) {
		if (!gameRenderer) return;
		gameRenderer.setZoom(Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, newZoom)));
	}

	// Debug cheat handlers
	function debugTeleport(x: number, y: number) {
		gState.player.x = x;
		gState.player.y = y;
		visualPlayerX = x;
		visualPlayerY = y;
		if (gameRenderer) {
			gameRenderer.setCamera(x / mapW, y / mapH);
		}

		syncCurrentSettlement();
		uploadEntityData();
	}

	function debugSetGold(amount: number) {
		gState.player.gold = Math.max(0, amount);
	}

	function debugSetSpeed(speed: number) {
		simSpeed = speed;
	}

	function debugHealPlayer() {
		gState.player.combatStats.currentHp = gState.player.combatStats.maxHp;
		gState.player.combatStats.currentMp = gState.player.combatStats.maxMp;
		gState.player.combatStats.currentSp = gState.player.combatStats.maxSp;
	}

	function debugSetZoom(zoom: number) {
		applyZoom(zoom);
	}

	function handleWheel(event: WheelEvent) {
		event.preventDefault();
		if (!gameRenderer) return;
		const zoom = gameRenderer.getZoom();
		const factor = event.deltaY > 0 ? 1.08 : 1 / 1.08;
		applyZoom(zoom * factor);
	}

	function getTouchDist(touches: TouchList): number {
		const dx = touches[0].clientX - touches[1].clientX;
		const dy = touches[0].clientY - touches[1].clientY;
		return Math.sqrt(dx * dx + dy * dy);
	}

	function handlePointerDown(event: PointerEvent) {
		if (event.button !== 0) return;
		isDragging = true;
		dragDistance = 0;
		panVelocityX = 0;
		panVelocityY = 0;
		dragStartX = event.clientX;
		dragStartY = event.clientY;
		if (gameRenderer) {
			dragCamStartX = gameRenderer.cameraX;
			dragCamStartY = gameRenderer.cameraY;
		}

		lastPointerX = event.clientX;
		lastPointerY = event.clientY;
		lastPointerTime = performance.now();
		(event.target as HTMLElement)?.setPointerCapture?.(event.pointerId);
	}

	function handlePointerMove(event: PointerEvent) {
		if (!isDragging || !gameRenderer || !canvas) return;
		dragDistance += Math.abs(event.clientX - lastPointerX) + Math.abs(event.clientY - lastPointerY);
		const rect = canvas.getBoundingClientRect();
		const zoom = gameRenderer.getZoom();
		const aspect = rect.width / rect.height;
		const viewW = zoom / mapW;
		const viewH = (zoom / aspect) / mapH;

		const dx = (event.clientX - dragStartX) / rect.width * viewW;
		const dy = (event.clientY - dragStartY) / rect.height * viewH;

		let cx = dragCamStartX - dx;
		let cy = dragCamStartY + dy;
		cx = ((cx % 1) + 1) % 1;
		cy = ((cy % 1) + 1) % 1;
		gameRenderer.setCamera(cx, cy);

		const now = performance.now();
		const elapsed = now - lastPointerTime;
		if (elapsed > 0) {
			panVelocityX = -(event.clientX - lastPointerX) / rect.width * viewW / (elapsed / 16);
			panVelocityY = (event.clientY - lastPointerY) / rect.height * viewH / (elapsed / 16);
		}

		lastPointerX = event.clientX;
		lastPointerY = event.clientY;
		lastPointerTime = now;
	}

	function handlePointerUp(event: PointerEvent) {
		isDragging = false;
		// If barely moved, treat as click
		if (dragDistance < 5) {
			handleCanvasClick(event);
		}
	}

	function handleTouchStart(event: TouchEvent) {
		if (event.touches.length === 2) {
			event.preventDefault();
			pinchStartDist = getTouchDist(event.touches);
			pinchStartZoom = gameRenderer?.getZoom() ?? 40;
		}
	}

	function handleTouchMove(event: TouchEvent) {
		if (event.touches.length === 2) {
			event.preventDefault();
			const dist = getTouchDist(event.touches);
			const ratio = pinchStartDist / dist;
			applyZoom(pinchStartZoom * ratio);
		}
	}

	function updatePanInertia() {
		if (isDragging) return;
		if (Math.abs(panVelocityX) < PAN_MIN_VELOCITY && Math.abs(panVelocityY) < PAN_MIN_VELOCITY) return;
		if (!gameRenderer) return;

		let cx = gameRenderer.cameraX + panVelocityX;
		let cy = gameRenderer.cameraY + panVelocityY;
		cx = ((cx % 1) + 1) % 1;
		cy = ((cy % 1) + 1) % 1;
		gameRenderer.setCamera(cx, cy);

		panVelocityX *= PAN_FRICTION;
		panVelocityY *= PAN_FRICTION;
	}
function enterSubworld(mode: 'city' | 'nature' = 'city') {
		if (!mapGenerator) return;
		let subSeed = gState.seed;
		let subDensity = 1000;

		if (mode === 'city' && currentSettlement) {
			subSeed += currentSettlement.id * 123;
			subDensity = currentSettlement.population;
		} else {
			subSeed += (gState.player.x * 1000 + gState.player.y);
			subDensity = 2000;
		}

		const gen = new CityGenerator(subSeed, 1024, 1024, mode);
		const data = gen.generate(subDensity);
		cityData = data;
		cityTraversability = gen.getTraversabilityData();

		// Синхронизируем рендерер с размером подмира
		gameRenderer?.updateMapDimensions(data.width, data.height);

		const gl = mapGenerator.getGL();
		if (cityTexture) gl.deleteTexture(cityTexture);
		const tex = gl.createTexture();
		if (tex) {
			gl.bindTexture(gl.TEXTURE_2D, tex);
			gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, data.visual);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
			cityTexture = tex;
		}

		overworldPlayerX = gState.player.x;
		overworldPlayerY = gState.player.y;
		gState.player.x = data.spawnX;
		gState.player.y = data.spawnY;
		visualPlayerX = data.spawnX;
		visualPlayerY = data.spawnY;
		movePath = [];
		moveIndex = 0;
		inCity = true;
		cityNpcs = spawnCityNPCs(subDensity, subSeed, data.grid, data.width, data.height);
		if (gameRenderer) gameRenderer.setZoom(mode === 'city' ? 60 : 80);
	}

	function leaveCity() {
		inCity = false;
		cityNpcs = [];
		// Возвращаем размеры глобальной карты в рендерер
		gameRenderer?.updateMapDimensions(mapW, mapH);
		gState.player.x = overworldPlayerX;
		gState.player.y = overworldPlayerY;
		visualPlayerX = overworldPlayerX;
		visualPlayerY = overworldPlayerY;
		movePath = [];
		moveIndex = 0;
		if (gameRenderer) gameRenderer.setZoom(40);
	}
</script>

<svelte:window onkeydown={handleKeyDown} onkeyup={handleKeyUp} />

<div class="relative h-full w-full">
	<!-- WebGL canvas -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<canvas
		bind:this={canvas}
		class="h-full w-full cursor-crosshair touch-none"
		style="image-rendering: pixelated;"
		onpointerdown={handlePointerDown}
		onpointermove={handlePointerMove}
		onpointerup={handlePointerUp}
		onpointercancel={handlePointerUp}
		onwheel={handleWheel}
		onmousemove={handleCanvasHover}
		onmouseleave={() => { hoverTileX = -1; hoverTileY = -1; hoveredNpc = undefined; }}
	></canvas>

	<!-- HUD top bar -->
	<div class="pointer-events-none absolute left-0 top-0 flex gap-4 bg-black/75 px-3 py-1.5 font-sans text-sm">
		<span class="text-blue-200">Time: {timeString}</span>
		<span class="text-yellow-400">Gold: {gState.player.gold}</span>
		<span class="text-red-400">HP: {gState.player.combatStats.currentHp}/{gState.player.combatStats.maxHp}</span>
		<span class="text-blue-400">MP: {gState.player.combatStats.currentMp}/{gState.player.combatStats.maxMp}</span>
		<span class="text-amber-300">SP: {Math.floor(gState.player.combatStats.currentSp)}/{gState.player.combatStats.maxSp}</span>
		<span class="text-white">Items: {gState.player.inventory.items.length}</span>
		{#if currentSettlementName}
			<span class="text-green-400">At: {currentSettlementName}</span>
		{/if}
		{#if hoveredNpc}
			<span class="text-purple-300">{hoveredNpc.name} (Lv.{hoveredNpc.level} HP:{hoveredNpc.hp}/{hoveredNpc.maxHp})</span>
		{/if}
	</div>
	<div class="pointer-events-none absolute left-0 top-7 flex gap-4 bg-black/75 px-3 py-0.5 font-sans text-xs text-gray-400">
		<span>Lv.{gState.player.levelData.level}</span>
		<span>Settlements: {gState.settlements.length}</span>
		<span>Day: {gState.worldTime.day}</span>
		{#if movePath.length > 0}
			<span class="text-blue-300">Moving... ({movePath.length - moveIndex} steps)</span>
		{/if}
	</div>

	<!-- City Badge in HUD -->
	{#if inCity && currentSettlement}
		<div class="pointer-events-none absolute left-3 top-14 flex items-center gap-3 rounded-lg border border-amber-900/40 bg-black/60 p-1.5 shadow-2xl animate-in fade-in slide-in-from-left-4">
			<div class="h-10 w-10 overflow-hidden rounded border border-gray-700 bg-gray-900">
				<img src={currentSettlement.banner} alt="Heraldry" class="h-full w-full object-cover" />
			</div>
			<div class="flex flex-col pr-2">
				<span class="text-[10px] font-bold uppercase tracking-widest text-amber-600">Current Location</span>
				<span class="text-xs font-black uppercase tracking-tight text-yellow-500">{currentSettlement.name}</span>
			</div>
		</div>
	{/if}

	<!-- Bottom controls -->
	<div class="absolute bottom-4 left-4 flex gap-2">
		<!-- Speed controls -->
		<button
			onclick={() => { simSpeed = 0; }}
			class="h-10 rounded px-3 font-sans text-sm text-white transition {simSpeed === 0 ? 'bg-cyan-700' : 'bg-slate-800/80 hover:bg-slate-700'}"
		>||</button>
		<button
			onclick={() => { simSpeed = 1; }}
			class="h-10 rounded px-3 font-sans text-sm text-white transition {simSpeed === 1 ? 'bg-cyan-700' : 'bg-slate-800/80 hover:bg-slate-700'}"
		>&gt;</button>
		<button
			onclick={() => { simSpeed = 2; }}
			class="h-10 rounded px-3 font-sans text-sm text-white transition {simSpeed === 2 ? 'bg-cyan-700' : 'bg-slate-800/80 hover:bg-slate-700'}"
		>&gt;&gt;</button>

		<!-- Stat screen -->
		<button
			onclick={() => (showStat = !showStat)}
			class="h-10 rounded bg-slate-800/80 px-3 font-sans text-sm text-white hover:bg-slate-700"
			title="Character [C]"
		>C</button>

		<!-- Inventory -->
		<button
			onclick={() => (showInventory = !showInventory)}
			class="h-10 rounded bg-slate-800/80 px-3 font-sans text-sm text-white hover:bg-slate-700"
			title="Inventory [I]"
		>I</button>

		<!-- Map -->
		<button
			onclick={() => (showMap = !showMap)}
			class="h-10 rounded bg-slate-800/80 px-3 font-sans text-sm text-white hover:bg-slate-700"
			title="Map [M]"
		>M</button>

		<!-- Pause menu -->
		<button
			onclick={() => (paused = true)}
			class="h-10 rounded bg-slate-800/80 px-3 font-sans text-sm text-white hover:bg-slate-700"
			title="Pause [Esc]"
		>=</button>
	</div>

	<!-- Zoom controls -->
	<div class="absolute bottom-4 right-4 flex gap-2">
		<button onclick={zoomIn} class="h-10 w-10 rounded bg-slate-800/80 font-sans text-xl text-white hover:bg-slate-700">+</button>
		<button onclick={zoomOut} class="h-10 w-10 rounded bg-slate-800/80 font-sans text-xl text-white hover:bg-slate-700">&minus;</button>
	</div>

	<!-- Entry Hint -->
	{#if !inCity && !paused && !showStat && !showSettlement}
		{#if currentSettlementName}
			<button
					onclick={() => { showSettlement = true; }}
				class="absolute right-4 top-2 cursor-pointer rounded border border-yellow-600/50 bg-yellow-900/90 px-4 py-2 font-sans text-sm font-bold text-yellow-200 shadow-lg transition hover:bg-yellow-800 hover:text-white"
			>
					Visit {currentSettlementName} [E]
			</button>
		{:else}
			<button
				onclick={() => enterSubworld('nature')}
				class="absolute right-4 top-2 cursor-pointer rounded border border-green-600/50 bg-green-900/90 px-4 py-2 font-sans text-sm font-bold text-green-200 shadow-lg transition hover:bg-green-800 hover:text-white"
			>
				Explore Wilds [E]
			</button>
		{/if}
	{/if}

	<!-- Leave City Button -->
	{#if inCity && !paused && !showStat}
		<button
			onclick={leaveCity}
			class="absolute right-4 top-2 cursor-pointer rounded border border-red-600/50 bg-red-900/90 px-4 py-2 font-sans text-sm font-bold text-red-200 shadow-lg transition hover:bg-red-800 hover:text-white"
		>
			Leave City [E]
		</button>
	{/if}

	<!-- Settlement overlay -->
	{#if showSettlement && currentSettlement}
		<SettlementOverlay
			player={gState.player}
			settlement={currentSettlement}
			worldSeed={gState.seed}
			onClose={() => (showSettlement = false)}
			onEnter={() => {
				showSettlement = false;
				enterSubworld('city');
			}}
			onTrade={handleSettlementTrade}
		/>
	{/if}

	<!-- Stat overlay -->
	{#if showStat}
		<StatOverlay
			bind:player={gState.player}
			onClose={() => (showStat = false)}
		/>
	{/if}

	<!-- Inventory overlay (reuses StatOverlay for now) -->
	{#if showInventory}
		<StatOverlay
			bind:player={gState.player}
			onClose={() => (showInventory = false)}
		/>
	{/if}

	<!-- NPC Interaction overlay -->
	{#if interactingNpc}
		<InteractionOverlay
			bind:player={gState.player}
			npc={interactingNpc}
			onClose={handleInteractionClose}
			onFight={handleInteractionFight}
			onTrade={handleInteractionTrade}
		/>
	{/if}

	<!-- Trade overlay -->
	{#if tradeNpc}
		<TradeOverlay
			bind:player={gState.player}
			traderName={tradeNpc.npc.name}
			traderInventory={tradeNpc.inventory}
			traderTraits={tradeNpc.npc.traits}
			onClose={handleTradeClose}
		/>
	{/if}

	<!-- Settlement trade overlay -->
	{#if tradeSettlement}
		<TradeOverlay
			bind:player={gState.player}
			traderName={tradeSettlement.settlement.name}
			traderInventory={tradeSettlement.settlement.inventory}
			traderTraits={[]}
			onClose={handleTradeClose}
		/>
	{/if}

	<!-- Event overlay -->
	{#if activeEvent}
		<EventOverlay
			bind:player={gState.player}
			event={activeEvent}
			onClose={handleEventClose}
			onBattle={handleEventBattle}
		/>
	{/if}

	<!-- Battle overlay -->
	{#if battleInfo}
		<BattleOverlay
			bind:player={gState.player}
			enemyName={battleInfo.enemyName}
			enemyType={battleInfo.enemyType}
			enemyLevel={battleInfo.enemyLevel}
			onEnd={handleBattleEnd}
		/>
	{/if}

	<!-- Map overlay -->
	{#if showMap && mapGenerator}
		<MapOverlay
			{mapGenerator}
			mapWidth={mapW}
			mapHeight={mapH}
			onClose={() => (showMap = false)}
		/>
	{/if}

	<!-- Pause overlay -->
	{#if paused}
		<PauseOverlay
			onResume={handleResume}
			onSave={handleSave}
			onLoad={handlePauseLoad}
			onCodex={() => { paused = false; showCodex = true; }}
			onToTitle={handleToTitle}
		/>
	{/if}

	<!-- Codex overlay -->
	{#if showCodex}
		<CodexOverlay player={gState.player} onClose={() => (showCodex = false)} />
	{/if}

	<!-- Debug overlay -->
	{#if showDebug}
		<DebugOverlay
			data={{
				gState,
				npcs,
				cityNpcs,
				inCity,
				trees,
				mapW,
				mapH,
				visualPlayerX,
				visualPlayerY,
				fps: debugFps,
				frameDt: debugFrameDt,
				simSpeed,
				zoom: gameRenderer?.getZoom() ?? 0,
				cameraX: gameRenderer?.cameraX ?? 0,
				cameraY: gameRenderer?.cameraY ?? 0,
				canvasW: canvas?.width ?? 0,
				canvasH: canvas?.height ?? 0,
				dpr: typeof window !== 'undefined' ? (window.devicePixelRatio || 1) : 1,
				atlasUploaded: gameRenderer?.getCharacterRenderer()?.isAtlasUploaded ?? false,
			}}
			onClose={() => { showDebug = false; }}
			onTeleport={debugTeleport}
			onSetGold={debugSetGold}
			onSetSpeed={debugSetSpeed}
			onHealPlayer={debugHealPlayer}
			onSetZoom={debugSetZoom}
		/>
	{/if}
</div>

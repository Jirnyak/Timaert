<script lang="ts">
	import {onMount} from 'svelte';
	import {type GameState, createGameState, saveGame} from '../game/state';
	import {MapGenerator} from '../webgl/map-generator';
	import {GameRenderer, type EntityData, SPRITE_PLAYER, SPRITE_TREE} from '../game/renderer';
	import {findPath} from '../game/pathfinding';
	import {
		type NPC,
		NPCType,
		spawnNPCs,
		tickNPCs,
		spriteFromNPC,
		SPRITE_CITY,
		spawnCityNPCs,
		tickCityNPCs,
	} from '../game/npc';
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
	import {type RandomEvent, rollForEvent} from '../game/events';
	import {type Inventory, createInventory, generateNpcInventory} from '../game/items';

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
	let showStat = $state(false);
	let showInventory = $state(false);
	let showSettlement = $state(false);
	let showMap = $state(false);
	let canvas: HTMLCanvasElement;
	let mapGenerator: MapGenerator | undefined;
	let gameRenderer: GameRenderer | undefined;
	let animFrameId = 0;
	let movePath: Array<{x: number; y: number}> = $state([]);
	let moveIndex = $state(0);
	let moveTimer = 0;
	let simSpeed = $state(1); // 0=paused, 1=normal, 2=fast
	let hoverTileX = -1;
	let hoverTileY = -1;
	let hoveredNpc: NPC | undefined = $state(undefined);
	let activeEvent: RandomEvent | undefined = $state(undefined);
	let battleInfo: {enemyName: string; enemyType: NPCType; enemyLevel: number} | undefined = $state(undefined);
	let interactingNpc: NPC | undefined = $state(undefined);
	let tradeNpc: {npc: NPC; inventory: Inventory} | undefined = $state(undefined);
	
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
	let worldTimeAccumulator = 0;
	const NPC_TICK_INTERVAL = 500; // ms between NPC movement ticks
	const MOVE_INTERVAL = 80; // ms per tile movement
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

		// Load sprite atlas and terrain textures
		await gameRenderer.loadSprites();
		await gameRenderer.loadTerrainTextures();

		// Scatter trees on traversable land (not near settlements/roads)
		trees = spawnTrees(gState.seed);

		// Spawn NPCs (after trees, so isLand checker is available)
		npcs = spawnNPCs(
			gState.settlements, gState.seed, mapW, mapH,
			(x, y) => mapGenerator?.isTraversable(x, y) ?? false,
		);
		console.log('[Trees] spawned', trees.length, 'first 5:', trees.slice(0, 5));

		uploadEntityData();

		startLoop();
	}

	function startLoop() {
		let lastTime = performance.now();

		function frame(now: number) {
			const dt = now - lastTime;
			lastTime = now;

			if (!paused && simSpeed > 0) {
				const scaledDt = dt * simSpeed;
				updateMovement(scaledDt);
				updateNPCs(scaledDt);
				updateWorldTime(scaledDt);
				updateNightDarken();
				if (Math.abs(gState.player.x - visualPlayerX) > 2 || Math.abs(gState.player.y - visualPlayerY) > 2) {
					visualPlayerX = gState.player.x;
					visualPlayerY = gState.player.y;
				} else {
					visualPlayerX += (gState.player.x - visualPlayerX) * 0.2 * simSpeed;
					visualPlayerY += (gState.player.y - visualPlayerY) * 0.2 * simSpeed;
				}
			}

			updatePanInertia();
			renderFrame();
			animFrameId = requestAnimationFrame(frame);
		}

		animFrameId = requestAnimationFrame(frame);
	}

	function updateMovement(dt: number) {
		if (movePath.length === 0 || moveIndex >= movePath.length) {
			return;
		}

		moveTimer += dt;
		while (moveTimer >= MOVE_INTERVAL && moveIndex < movePath.length) {
			moveTimer -= MOVE_INTERVAL;
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
				break;
			}
		}

		// Smooth camera follow
		if (gameRenderer) {
			const currentMapW = inCity && cityData ? cityData.width : mapW;
			const currentMapH = inCity && cityData ? cityData.height : mapH;
			
			gameRenderer.setCamera(
				visualPlayerX / currentMapW,
				visualPlayerY / currentMapH,
			);
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

		// NPCs (Мир или Город/Лес выбираются автоматически)
		const activeNpcList = inCity ? cityNpcs : npcs;
		for (const npc of activeNpcList) {
			if (npc.hp > 0) {
				entities.push({
					x: npc.x,
					y: npc.y,
					type: spriteFromNPC(npc.type),
					active: true,
					scale: 1.2,
				});
			}
		}

		// Player (drawn last = on top)
		entities.push({
			x: visualPlayerX,
			y: visualPlayerY,
			type: SPRITE_PLAYER,
			active: true,
			scale: 1.4,
		});

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
	}

	function handleBattleEnd(_victory: boolean) {
		battleInfo = undefined;
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
	}

	function handleInteractionTrade() {
		const npc = interactingNpc;
		if (!npc) {
			return;
		}

		interactingNpc = undefined;
		const rng = () => Math.random();
		const inv = createInventory(24);
		const items = generateNpcInventory(npc.type, npc.level, rng);
		for (const item of items) {
			inv.items.push(item);
		}

		tradeNpc = {npc, inventory: inv};
	}

	function handleTradeClose() {
		tradeNpc = undefined;
	}

	function renderFrame() {
		if (!gameRenderer || !mapGenerator || !canvas) {
			return;
		}

		const w = canvas.clientWidth;
		const h = canvas.clientHeight;
		canvas.width = w;
		canvas.height = h;

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
		// Передаем визуальные координаты в метод render для синхронизации слоев
		gameRenderer.render(texture_to_render, visualPlayerX, visualPlayerY, w, h, hoverTileX, hoverTileY);
	}

	function handleCanvasClick(event: MouseEvent) {
		if (paused || activeEvent || battleInfo || interactingNpc || tradeNpc || showStat || showInventory || showSettlement || !gameRenderer || !mapGenerator) {
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
			moveTimer = 0;
		}
	}

	function handleKeyDown(event: KeyboardEvent) {
		if (event.key === 'Escape') {
			if (showStat) {
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
					// Проверка: стоим ли мы на лесу/природе?
					// Для прототипа позволяем входить везде в лесной режим
					enterSubworld('nature');
				}
			}

			return;
		}

		if (paused || showStat || showInventory || showSettlement || activeEvent || battleInfo || interactingNpc || tradeNpc) {
			return;
		}

		const dirMap: Record<string, [number, number]> = {
			ArrowUp: [0, 1],
			ArrowDown: [0, -1],
			ArrowLeft: [-1, 0],
			ArrowRight: [1, 0],
			w: [0, 1],
			s: [0, -1],
			a: [-1, 0],
			d: [1, 0],
		};

		const dir = dirMap[event.key];
		if (!dir) {
			return;
		}

		event.preventDefault();
		movePath = [];
		moveIndex = 0;

		const currentMapW = inCity && cityData ? cityData.width : mapW;
		const currentMapH = inCity && cityData ? cityData.height : mapH;

		let nx = gState.player.x + dir[0];
		let ny = gState.player.y + dir[1];

		if (inCity) {
			// В городе выход за границы = возвращение на карту мира (Seamless Exit)
			if (nx < 0 || nx >= currentMapW || ny < 0 || ny >= currentMapH) {
				leaveCity();
				return;
			}
		} else {
			// Глобальная карта зациклена (Torus)
			nx = (nx % mapW + mapW) % mapW;
			ny = (ny % mapH + mapH) % mapH;
		}

		// Проверка проходимости с учетом контекста
		const isWalkable = inCity 
			? (cityTraversability?.data[ny * currentMapW + nx] ?? 0) > 127
			: (mapGenerator?.isTraversable(nx, ny) ?? false);

		if (isWalkable) {
			gState.player.x = nx;
			gState.player.y = ny;
			stepsSincLastEvent++;

			if (gameRenderer) {
				gameRenderer.setCamera(nx / currentMapW, ny / currentMapH);
			}

			if (!inCity) syncCurrentSettlement();

			const evt = rollForEvent(stepsSincLastEvent);
			if (evt) {
				activeEvent = evt;
				stepsSincLastEvent = 0;
			}
		}
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

<svelte:window onkeydown={handleKeyDown} />

<div class="relative h-full w-full">
	<!-- WebGL canvas -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<canvas
		bind:this={canvas}
		class="h-full w-full cursor-crosshair touch-none"
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
			onToTitle={handleToTitle}
		/>
	{/if}
</div>

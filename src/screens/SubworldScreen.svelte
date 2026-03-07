<script lang="ts">
	import {onMount} from 'svelte';
	import type {PlayerState, Settlement} from '../game/state';
	import {
		SubworldEngine, SubworldRenderer, seededRng, findWalkable, makeEntity,
		createCitizenSpriteSheet, renderPlayerSprite,
	} from '../game/subworld';
	import type {
		SubworldConfig, SubworldEntity, TraversabilityGrid,
		ZoneAction, BattleSubworldOptions, BattleResult,
	} from '../game/subworld';
	import {CityGenerator, TILE_ROAD, TILE_SQUARE} from '../game/city-generator';
	import {countSurvivors, totalUnits, UnitType, UNIT_STATS} from '../game/army';
	import type {ArmyComposition} from '../game/army';
	import {loadTrack, playTrack} from '../game/audio';
	import {color, btnProps, messageStyle, mutedStyle} from '../ui/theme';

	type Props = {
		player: PlayerState;
		settlement?: Settlement;
		seed: number;
		mode: 'city' | 'nature';
		battleOptions?: BattleSubworldOptions;
		onExit: () => void;
		onTrade: () => void;
		onBattleEnd?: (result: BattleResult) => void;
	};

	let {player, settlement, seed, mode, battleOptions, onExit, onTrade, onBattleEnd}: Props = $props();

	let canvas: HTMLCanvasElement;
	let message = $state('');
	let messageTimer = 0;
	let engine: SubworldEngine | undefined;
	let renderer: SubworldRenderer | undefined;
	let animFrame = 0;
	let loading = $state(true);
	let zoom = $state(1);
	let friendlyCount = $state(0);
	let enemyCount = $state(0);

	const ZOOM_MIN = 0.25;
	const ZOOM_MAX = 4;
	const ZOOM_STEP = 1.15;
	const MAP_SIZE = 1024;
	const CITY_SCALE = 40;
	const pressed = new Set<string>();
	const hasBattle = Boolean(battleOptions);

	const locationName = $derived(
		hasBattle
			? (battleOptions?.enemyName ?? 'Battle')
			: mode === 'city' && settlement
				? settlement.name
				: 'The Wilds',
	);

	// ── Config builders ─────────────────────────────────────────

	async function buildCityConfig(): Promise<SubworldConfig> {
		const s = settlement!;
		const cfgSeed = seed + s.id * 123;
		const generator = new CityGenerator(cfgSeed, MAP_SIZE, MAP_SIZE, 'city');
		const mapData = generator.generate(s.population);
		const rng = seededRng(cfgSeed + 7);

		const traversability: TraversabilityGrid = {
			width: mapData.width, height: mapData.height, data: mapData.grid,
		};

		const nextId = {value: 0};
		const entities: SubworldEntity[] = [];

		entities.push(makeEntity(nextId, {
			kind: 'player', x: mapData.spawnX, y: mapData.spawnY,
			radius: 0.5, solid: true, label: 'You', color: '#4af',
		}));

		// Exit zones at the four edges
		for (const pos of [
			{x: mapData.spawnX, y: 6},
			{x: mapData.spawnX, y: mapData.height - 6},
			{x: 6, y: mapData.spawnY},
			{x: mapData.width - 6, y: mapData.spawnY},
		]) {
			entities.push(makeEntity(nextId, {
				kind: 'zone', x: pos.x, y: pos.y, radius: 48,
				label: 'Exit', color: 'rgba(255,80,80,0.25)', action: {type: 'exit'},
			}));
		}

		// Trade zone near center square
		const tradeSpot = generator.findTileNear(mapData.spawnX, mapData.spawnY, TILE_SQUARE, 30);
		if (tradeSpot) {
			entities.push(makeEntity(nextId, {
				kind: 'zone', x: tradeSpot.x + 3, y: tradeSpot.y + 3, radius: 8,
				label: 'Market', color: 'rgba(255,255,100,0.2)', action: {type: 'trade'},
			}));
		}

		// Inn
		const innSpot = generator.findTileNear(mapData.spawnX + 20, mapData.spawnY - 15, TILE_ROAD, 40);
		if (innSpot) {
			const cost = s.mood === 'Prosperous' ? 5 : (s.mood === 'Tense' ? 15 : 10);
			entities.push(makeEntity(nextId, {
				kind: 'zone', x: innSpot.x, y: innSpot.y, radius: 8,
				label: 'Inn', color: 'rgba(100,200,255,0.2)', action: {type: 'rest', cost},
			}));
		}

		// NPCs
		const citizenSheet = await createCitizenSpriteSheet(s.population);
		const playerSheet = await renderPlayerSprite(player.characterData);
		const npcNames = ['Villager', 'Guard', 'Merchant', 'Peasant', 'Scholar', 'Beggar'];
		for (let i = 0; i < s.population; i++) {
			const spot = generator.findRoadNearHouses(rng);
			if (spot) {
				entities.push(makeEntity(nextId, {
					kind: 'npc', x: spot.x, y: spot.y, radius: 0.5, solid: true,
					label: npcNames[Math.floor(rng() * npcNames.length)],
					color: `hsl(${Math.floor(rng() * 360)}, 40%, 55%)`,
					ai: 'wander', aiTimer: rng() * 3,
					spriteIndex: i % citizenSheet.count,
				}));
			}
		}

		return {
			seed: cfgSeed, width: mapData.width, height: mapData.height,
			bgColor: '#3a4a2a', groundColorA: '#4a5a3a', groundColorB: '#3e5235',
			entities, name: s.name,
			bgImage: mapData.visual, traversability, scale: CITY_SCALE,
			citizenSheet, playerSheet,
		};
	}

	async function buildNatureConfig(): Promise<SubworldConfig> {
		const opts = battleOptions;
		const natureSeed = opts ? opts.seed : seed;
		const generator = new CityGenerator(natureSeed, MAP_SIZE, MAP_SIZE, 'nature');
		const mapData = generator.generate(500);
		const rng = seededRng(natureSeed + 13);
		const playerSheet = await renderPlayerSprite(player.characterData);

		const traversability: TraversabilityGrid = {
			width: mapData.width, height: mapData.height, data: mapData.grid,
		};

		const nextId = {value: 0};
		const entities: SubworldEntity[] = [];

		// Player entity — gains combat stats when armies are present
		if (opts) {
			entities.push({
				id: nextId.value++, kind: 'player',
				x: mapData.spawnX, y: mapData.spawnY, vx: 0, vy: 0,
				radius: 1.5, solid: true, label: 'You', color: '#4af',
				hp: opts.playerHp, maxHp: opts.playerMaxHp,
				team: 0, unitType: UnitType.Swordsman, attackTimer: 0,
			});
		} else {
			entities.push(makeEntity(nextId, {
				kind: 'player', x: mapData.spawnX, y: mapData.spawnY,
				radius: 0.5, solid: true, label: 'You', color: '#4af',
			}));
		}

		// Exit zones at the four edges
		for (const pos of [
			{x: mapData.spawnX, y: 6},
			{x: mapData.spawnX, y: mapData.height - 6},
			{x: 6, y: mapData.spawnY},
			{x: mapData.width - 6, y: mapData.spawnY},
		]) {
			entities.push(makeEntity(nextId, {
				kind: 'zone', x: pos.x, y: pos.y, radius: 48,
				label: 'Exit', color: 'rgba(255,80,80,0.25)', action: {type: 'exit'},
			}));
		}

		// Spawn armies when battleOptions are present
		if (opts) {
			const allyCenter = findWalkable(traversability, rng, mapData.spawnX - 30, mapData.spawnY, 40)
				?? {x: mapData.spawnX - 30, y: mapData.spawnY};
			const enemyCenter = findWalkable(traversability, rng, mapData.spawnX + 60, mapData.spawnY, 40)
				?? {x: mapData.spawnX + 60, y: mapData.spawnY};
			spawnFormation(entities, nextId, opts.playerArmy, 0, allyCenter.x, allyCenter.y, rng);
			spawnFormation(entities, nextId, opts.enemyArmy, 1, enemyCenter.x, enemyCenter.y, rng);
		} else {
			// Wildlife NPCs only in peaceful exploration
			const creatureNames = ['Deer', 'Wolf', 'Rabbit', 'Fox', 'Bear'];
			const count = 4 + Math.floor(rng() * 4);
			for (let i = 0; i < count; i++) {
				const spot = findWalkable(traversability, rng, mapData.spawnX, mapData.spawnY, Math.min(300, mapData.width / 2));
				if (spot) {
					entities.push(makeEntity(nextId, {
						kind: 'npc', x: spot.x, y: spot.y, radius: 0.5, solid: true,
						label: creatureNames[Math.floor(rng() * creatureNames.length)],
						color: `hsl(${Math.floor(rng() * 120)}, 35%, 45%)`,
						ai: 'wander', aiTimer: rng() * 3,
					}));
				}
			}

			const clearingSpot = generator.findTileNear(mapData.spawnX, mapData.spawnY, TILE_SQUARE, 60);
			if (clearingSpot) {
				entities.push(makeEntity(nextId, {
					kind: 'zone', x: clearingSpot.x, y: clearingSpot.y, radius: 40,
					label: 'Clearing', color: 'rgba(100,255,100,0.15)',
					action: {type: 'dialog', text: 'A peaceful clearing in the woods. You can rest here.'},
				}));
			}
		}

		const name = opts ? `Battle: ${opts.enemyName}` : 'The Wilds';

		return {
			seed: natureSeed, width: mapData.width, height: mapData.height,
			bgColor: '#1a2a0a', groundColorA: '#2a3a1a', groundColorB: '#253215',
			entities, name,
			bgImage: mapData.visual, traversability, scale: CITY_SCALE,
			playerSheet, playerDamage: opts?.playerDamage,
		};
	}

	function spawnFormation(
		entities: SubworldEntity[], nextId: {value: number},
		army: ArmyComposition, team: number,
		centerX: number, centerY: number, rng: () => number,
	): void {
		const units: Array<[number, number]> = [
			[UnitType.Swordsman, army.swordsmen],
			[UnitType.Spearman, army.spearmen],
			[UnitType.Horseman, army.horsemen],
			[UnitType.Archer, army.archers],
		];
		const dir = team === 0 ? 1 : -1;
		let row = 0;
		for (const [type, unitCount] of units) {
			const isRanged = type === UnitType.Archer;
			const rankOffset = isRanged ? -12 * dir : 0;
			for (let i = 0; i < unitCount; i++) {
				const stats = UNIT_STATS[type as UnitType];
				const x = centerX + rankOffset + (rng() - 0.5) * 8;
				const y = centerY - 15 + row * 5 + (rng() - 0.5) * 3;
				entities.push({
					id: nextId.value++, kind: 'soldier', x, y, vx: 0, vy: 0,
					radius: 1.5, solid: true, label: stats.label,
					color: team === 0 ? '#44aa99' : '#cc4444',
					hp: stats.hp, maxHp: stats.hp, team, unitType: type,
					attackTimer: rng() * stats.cooldown,
				});
				row++;
			}
		}
	}

	// ── Unified exit ────────────────────────────────────────────

	function exitSubworld() {
		if (onBattleEnd) {
			const ents = engine?.entities ?? [];
			const surviving = countSurvivors(ents, 0);
			const enemySurviving = countSurvivors(ents, 1);
			onBattleEnd({
				victory: totalUnits(surviving) > 0 && totalUnits(enemySurviving) === 0,
				survivingArmy: surviving,
				enemySurviving,
				playerHp: Math.max(0, engine?.player.hp ?? 0),
			});
		}

		onExit();
	}

	// ── Lifecycle ───────────────────────────────────────────────

	onMount(() => {
		const trackName = hasBattle ? 'battle' : 'subworld';
		const trackPath = hasBattle ? '/assets/sound/empire-theme.mp3' : '/assets/sound/subworld.mp3';
		void loadTrack(trackName, trackPath).then(() => {
			void playTrack(trackName);
		});

		const configPromise = mode === 'city'
			? buildCityConfig()
			: buildNatureConfig();

		let cancelled = false;

		configPromise.then(config => {
			if (cancelled) return;

			loading = false;
			engine = new SubworldEngine(config);
			renderer = new SubworldRenderer(canvas);
			let lastTime = performance.now();

			function frame(now: number) {
				const dt = (now - lastTime) / 1000;
				lastTime = now;

				if (engine) {
					engine.inputDir = {
						x: (pressed.has('d') || pressed.has('ArrowRight') ? 1 : 0)
							- (pressed.has('a') || pressed.has('ArrowLeft') ? 1 : 0),
						y: (pressed.has('s') || pressed.has('ArrowDown') ? 1 : 0)
							- (pressed.has('w') || pressed.has('ArrowUp') ? 1 : 0),
					};

					engine.tick(dt);

					const action = engine.consumeAction();
					if (action) handleAction(action);

					if (hasBattle) {
						friendlyCount = engine.entities.filter(e => e.kind === 'soldier' && e.team === 0 && (e.hp ?? 0) > 0).length;
						enemyCount = engine.entities.filter(e => e.kind === 'soldier' && e.team === 1 && (e.hp ?? 0) > 0).length;
					}

					if (renderer) {
						const effectiveScale = (engine.config.scale || 40) * zoom;
						renderer.render(engine.config, engine.player.x, engine.player.y, effectiveScale);
					}
				}

				if (message && messageTimer > 0) {
					messageTimer -= dt;
					if (messageTimer <= 0) message = '';
				}

				animFrame = requestAnimationFrame(frame);
			}

			animFrame = requestAnimationFrame(frame);
		});

		return () => {
			cancelled = true;
			cancelAnimationFrame(animFrame);
			void playTrack('explore');
		};
	});

	function handleAction(action: ZoneAction) {
		switch (action.type) {
			case 'exit': { exitSubworld(); break; }
			case 'trade': { onTrade(); break; }
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

			case 'dialog': { showMessage(action.text); break; }
			default: break;
		}
	}

	function showMessage(text: string) {
		message = text;
		messageTimer = 3;
	}

	function handleKeyDown(event: KeyboardEvent) {
		if (event.key === 'Escape') {
			exitSubworld();
			return;
		}

		pressed.add(event.key);
	}

	function handleKeyUp(event: KeyboardEvent) {
		pressed.delete(event.key);
	}

	function handleWheel(event: WheelEvent) {
		event.preventDefault();
		if (event.deltaY < 0) zoom = Math.min(ZOOM_MAX, zoom * ZOOM_STEP);
		else zoom = Math.max(ZOOM_MIN, zoom / ZOOM_STEP);
	}
</script>

<svelte:window onkeydown={handleKeyDown} onkeyup={handleKeyUp} />

<div class="absolute inset-0 z-[100] flex flex-col" style="background: #1a1a1a;">
	<!-- HUD -->
	<div class="flex items-center justify-between bg-black/80 px-4 py-2 font-sans text-sm">
		<div class="flex items-center gap-4">
			<span class="font-bold uppercase tracking-wider" style="color: {color.accent};">{locationName}</span>
			<span style="color: {color.hp};">HP: {player.combatStats.currentHp}/{player.combatStats.maxHp}</span>
			<span style="color: {color.mp};">MP: {player.combatStats.currentMp}/{player.combatStats.maxMp}</span>
			<span style="color: {color.sp};">SP: {Math.floor(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
			<span class="text-yellow-400">Gold: {player.gold}</span>
			{#if hasBattle}
				<span class="text-green-400">Allies: {friendlyCount}</span>
				<span class="text-red-400">Enemies: {enemyCount}</span>
			{/if}
		</div>
		{#if hasBattle}
			<button onclick={exitSubworld}
				class="rounded border-2 px-3 py-1 text-xs font-bold uppercase tracking-wide transition"
				{...btnProps('close')}
			>Retreat [Esc]</button>
		{:else}
			<button onclick={exitSubworld}
				class="rounded border-2 px-3 py-1 text-xs font-bold uppercase tracking-wide transition"
				{...btnProps('close')}
			>Leave [Esc]</button>
		{/if}
	</div>

	<!-- Canvas -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div class="relative flex-1" onwheel={handleWheel}>
		<canvas bind:this={canvas} class="h-full w-full" style="image-rendering: pixelated;"></canvas>

		{#if loading}
			<div class="absolute inset-0 flex items-center justify-center bg-black/80">
				<span class="font-sans text-sm" style={mutedStyle}>Generating world...</span>
			</div>
		{/if}

		<div class="pointer-events-none absolute bottom-4 left-4 rounded bg-black/60 px-3 py-2 font-sans text-xs" style={mutedStyle}>
			WASD / Arrows to move · Walk into zones to interact
		</div>

		{#if message}
			<div class="absolute bottom-4 left-1/2 -translate-x-1/2 rounded border px-4 py-2 text-center font-sans text-sm shadow-lg" style={messageStyle}>
				{message}
			</div>
		{/if}
	</div>
</div>

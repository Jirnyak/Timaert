<script lang="ts">
	import {onMount} from 'svelte';
	import type {PlayerState, Settlement, GameState} from '../game/state';
	import {
		SubworldEngine, SubworldRenderer, findWalkable, makeEntity,
		createCitizenSpriteSheet, renderPlayerSprite,
		spawnArmy, spawnCityNpcs, spawnWildernessNpcs,
		type SubworldConfig, type SubworldEntity, type TraversabilityGrid,
		type ZoneAction, type SubworldResult, type FightContext,
	} from '../game/subworld';
	import {xorshift32} from '../game/rng';
		import {generateSubworldMap} from '../game/subworld/map-factory';
	import {
		type SubworldMode, TILE_ROAD, TILE_SQUARE, findTileNear, findRoadNearHouses,
	} from '../game/subworld/map-data';
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
	import DebugOverlay from './DebugOverlay.svelte';

	type Props = {
		player: PlayerState;
		gameState: GameState;
		settlement?: Settlement;
		seed: number;
		mode: SubworldMode;
		fightContext?: FightContext;
		onExit: (result?: SubworldResult) => void;
		onTrade: () => void;
	};

	let {player = $bindable(), gameState, settlement, seed, mode, fightContext, onExit, onTrade}: Props = $props();

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
	let paused = $state(false);
	let showDebug = $state(false);

	// Spell casting state
	let mouseWorldX = 0;
	let mouseWorldY = 0;

	const activeSpell = $derived(getSpell(player.spellBook.activeSpellId));

	const ZOOM_MIN = 0.25;
	const ZOOM_MAX = 4;
	const ZOOM_STEP = 1.15;
	const MAP_SIZE = 1024;
	const CITY_SCALE = 40;
	const pressed = new Set<string>();

	const isUrban = $derived(mode === 'city' || mode === 'village');

	const locationName = $derived(isUrban && settlement
		? settlement.name
		: 'The Wilds');

	// ── Helpers ──────────────────────────────────────────────────

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

	// ── Config builders ─────────────────────────────────────────

	async function buildCityConfig(): Promise<SubworldConfig> {
		const s = settlement!;
		const cfgSeed = seed + s.id * 123;
		const mapData = generateSubworldMap(cfgSeed, MAP_SIZE, MAP_SIZE, mode, s.population);
		const rng = xorshift32(cfgSeed + 7);

		const traversability: TraversabilityGrid = {
			width: mapData.width, height: mapData.height, data: mapData.grid,
		};

		const nextId = {value: 0};
		const entities: SubworldEntity[] = [];

		// Player with RPG-derived combat stats
		entities.push(makeEntity(nextId, {
			kind: 'player', x: mapData.spawnX, y: mapData.spawnY,
			radius: 0.5, solid: true, label: 'You', color: '#4af',
			hp: player.combatStats.currentHp,
			maxHp: player.combatStats.maxHp,
			team: 0,
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
		const tradeSpot = findTileNear(mapData.tileGrid, mapData.width, mapData.height, mapData.spawnX, mapData.spawnY, TILE_SQUARE, 30);
		if (tradeSpot) {
			entities.push(makeEntity(nextId, {
				kind: 'zone', x: tradeSpot.x + 3, y: tradeSpot.y + 3, radius: 8,
				label: 'Market', color: 'rgba(255,255,100,0.2)', action: {type: 'trade'},
			}));
		}

		// Inn
		const innSpot = findTileNear(mapData.tileGrid, mapData.width, mapData.height, mapData.spawnX + 20, mapData.spawnY - 15, TILE_ROAD, 40);
		if (innSpot) {
			const cost = s.mood === 'Prosperous' ? 5 : (s.mood === 'Tense' ? 15 : 10);
			entities.push(makeEntity(nextId, {
				kind: 'zone', x: innSpot.x, y: innSpot.y, radius: 8,
				label: 'Inn', color: 'rgba(100,200,255,0.2)', action: {type: 'rest', cost},
			}));
		}

		// Determine city faction from settlement position
		const cityFaction = settlementFaction(s.x, s.y);

		// NPCs — populated from NPC template registry with combat stats
		const citizenSheet = await createCitizenSpriteSheet(s.population);
		const playerSheet = await renderPlayerSprite(player.characterData);

		// NPC type distribution for city population
		const npcDistribution: Array<{type: NPCType; weight: number}> = [
			{type: NPCType.Peasant, weight: 0.55},
			{type: NPCType.Merchant, weight: 0.2},
			{type: NPCType.Woodcutter, weight: 0.2},
			{type: NPCType.Witch, weight: 0.05},
			{type: NPCType.Guard, weight: 0},
			{type: NPCType.Sorceress, weight: 0},
		];

		const guardTypes = new Set([NPCType.Guard, NPCType.Sorceress]);

		// Spawn city NPCs — derived from macro NPC templates via unified path
		entities.push(...spawnCityNpcs(
			s.population,
			cityFaction,
			npcDistribution,
			guardTypes,
			nextId,
			rng,
			() => findRoadNearHouses(mapData.tileGrid, mapData.width, mapData.height, rng),
			citizenSheet.count,
		));

		const fc = factionContext();
		return {
			seed: cfgSeed, width: mapData.width, height: mapData.height,
			bgColor: '#3a4a2a', groundColorA: '#4a5a3a', groundColorB: '#3e5235',
			entities, name: s.name,
			bgImage: mapData.visual, traversability, scale: CITY_SCALE,
			citizenSheet, playerSheet,
			playerDamage: playerDamage(),
			playerRange: 5,
			playerCooldown: 0.5,
			factions: fc.factions,
			playerReputation: fc.reputation,
		};
	}

	async function buildNatureConfig(): Promise<SubworldConfig> {
		const natureSeed = seed;
		const mapData = generateSubworldMap(natureSeed, MAP_SIZE, MAP_SIZE, mode, 500);
		const rng = xorshift32(natureSeed + 13);
		const playerSheet = await renderPlayerSprite(player.characterData);

		const traversability: TraversabilityGrid = {
			width: mapData.width, height: mapData.height, data: mapData.grid,
		};

		const nextId = {value: 0};
		const entities: SubworldEntity[] = [];

		// Player entity — RPG-derived combat stats
		entities.push({
			id: nextId.value++, kind: 'player',
			x: mapData.spawnX, y: mapData.spawnY, vx: 0, vy: 0,
			radius: 1.5, solid: true, label: 'You', color: '#4af',
			hp: player.combatStats.currentHp, maxHp: player.combatStats.maxHp,
			attackTimer: 0,
		});

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

		// ── Hostile mobs — bandits (cults faction, always hostile) ──
		const banditCount = 3 + Math.floor(rng() * 5);
		entities.push(...spawnWildernessNpcs(NPCType.Bandit, banditCount, 'cults', '#cc4444', nextId, traversability, rng, mapData.spawnX + 80, mapData.spawnY, 150));

		// Wildlife (harmless wanderers — no faction, no hp)
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

		// ── Army spawning (fight interaction) ───────────────────
		if (fightContext) {
			const unitColors: Record<number, string> = {
				0: '#4488ff', 1: '#44cc44', 2: '#aaaa44', 3: '#cc8844',
			};

			const enemyColors: Record<number, string> = {
				0: '#cc4444', 1: '#cc6644', 2: '#884444', 3: '#cc4488',
			};

			// Player + enemy armies — soldiers derived from macro army via unified path
			entities.push(
				...spawnArmy(fightContext.playerArmy, 'playerArmy', '', unitColors, mapData.spawnX, mapData.spawnY, 40, nextId, traversability, rng),
				...spawnArmy(fightContext.enemyArmy, fightContext.enemyFactionId, fightContext.enemyName, enemyColors, mapData.spawnX + 160, mapData.spawnY, 40, nextId, traversability, rng),
			);
		}

		const clearingSpot = findTileNear(mapData.tileGrid, mapData.width, mapData.height, mapData.spawnX, mapData.spawnY, TILE_SQUARE, 60);
		if (clearingSpot) {
			entities.push(makeEntity(nextId, {
				kind: 'zone', x: clearingSpot.x, y: clearingSpot.y, radius: 40,
				label: 'Clearing', color: 'rgba(100,255,100,0.15)',
				action: {type: 'dialog', text: 'A peaceful clearing in the woods. You can rest here.'},
			}));
		}

		const fc = factionContext();

		// When fighting, ensure enemy faction is hostile and player_army opposes them
		if (fightContext) {
			const enemyFac = fightContext.enemyFactionId;
			fc.reputation[enemyFac] = -100;
			fc.factions.playerArmy ??= {relations: {}};
			fc.factions.playerArmy.relations[enemyFac] = -100;
			fc.factions[enemyFac] ??= {relations: {}};
			fc.factions[enemyFac].relations.playerArmy = -100;
		}

		return {
			seed: natureSeed, width: mapData.width, height: mapData.height,
			bgColor: '#1a2a0a', groundColorA: '#2a3a1a', groundColorB: '#253215',
			entities, name: 'The Wilds',
			bgImage: mapData.visual, traversability, scale: CITY_SCALE,
			playerSheet,
			playerDamage: playerDamage(),
			playerRange: 5,
			playerCooldown: 0.5,
			factions: fc.factions,
			playerReputation: fc.reputation,
		};
	}

	// ── Unified exit ────────────────────────────────────────────

	function exitSubworld() {
		const result = engine?.getResult();
		onExit(result);
	}

	// ── Lifecycle ───────────────────────────────────────────────

	onMount(() => {
		// eslint-disable-next-line promise/prefer-await-to-then
		loadTrack('subworld', '/assets/sound/subworld.mp3').then(() => playTrack('subworld')).catch(() => {});

		const configPromise = isUrban
			? buildCityConfig()
			: buildNatureConfig();

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
			let lastTime = performance.now();

			function frame(now: number) {
				const dt = (now - lastTime) / 1000;
				lastTime = now;

				if (engine && !paused) {
					engine.inputDir = {
						x: (pressed.has('ArrowRight') ? 1 : 0)
							- (pressed.has('ArrowLeft') ? 1 : 0),
						y: (pressed.has('ArrowDown') ? 1 : 0)
							- (pressed.has('ArrowUp') ? 1 : 0),
					};
					engine.attackHeld = pressed.has('a') || pressed.has('A');
					engine.playerFlying = player.spellBook.sustainedActive.includes('flight');

					engine.tick(dt);
					tickSpellBook(player.spellBook, player.combatStats, dt, getSpell);

					// Sync player HP back to macroworld state
					if (engine.player.hp !== undefined) {
						player.combatStats.currentHp = Math.max(0, engine.player.hp);
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

					if (renderer) {
						const effectiveScale = (engine.config.scale || 40) * zoom;
						renderer.render(engine.config, engine.player.x, engine.player.y, effectiveScale);

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
		});

		return () => {
			cancelled = true;
			cancelAnimationFrame(animFrame);
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
		if (!engine || !canvas) {
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

	function tryCastSpell() {
		if (!engine || !activeSpell) {
			return;
		}

		const check = canCast(activeSpell, player.combatStats, player.spellBook, true);
		if (!check.ok) {
			showMessage(check.reason);
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
		const cast = engine.castSpell(dmg, projSpeed, projRadius, blast, activeSpell.micro?.friendlyFire ?? false, mouseWorldX, mouseWorldY, spellColor);
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
	</div>

	<!-- Canvas -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div class="relative flex-1" onwheel={handleWheel} onmousemove={handleMouseMove}>
		<canvas bind:this={canvas} class="h-full w-full" style="image-rendering: pixelated;"></canvas>

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
			Arrows to move · A to attack · S to cast spell · Space to pause
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
				inCity: isUrban,
				trees: [],
				mapW: MAP_SIZE,
				mapH: MAP_SIZE,
				visualPlayerX: engine?.player.x ?? 0,
				visualPlayerY: engine?.player.y ?? 0,
				fps: 0,
				frameDt: 0,
				simSpeed: paused ? 0 : 1,
				zoom,
				cameraX: engine?.player.x ?? 0,
				cameraY: engine?.player.y ?? 0,
				canvasW: canvas?.width ?? 0,
				canvasH: canvas?.height ?? 0,
				dpr: globalThis.window === undefined ? 1 : (window.devicePixelRatio || 1),
				atlasUploaded: false,
			}}
			onClose={() => {
				showDebug = false;
			}}
			onHealPlayer={debugHealPlayer}
			onLearnAllSpells={debugLearnAllSpells}
			onAddExp={debugAddExp}
		/>
	{/if}
</div>

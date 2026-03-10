<script lang="ts">
	import {onMount} from 'svelte';
	import type {PlayerState, Settlement, GameState} from '../game/state';
	import {
		SubworldEngine, SubworldRenderer, seededRng, findWalkable, makeEntity,
		createCitizenSpriteSheet, renderPlayerSprite,
	} from '../game/subworld';
	import type {
		SubworldConfig, SubworldEntity, TraversabilityGrid,
		ZoneAction, SubworldResult, FightContext,
	} from '../game/subworld';
	import {generateSubworldMap} from '../game/subworld/map-factory';
	import type {SubworldMode} from '../game/subworld/map-data';
	import {TILE_ROAD, TILE_SQUARE, findTileNear, findRoadNearHouses} from '../game/subworld/map-data';
	import {NPC_TYPE_DEFS, NPCType, settlementFaction} from '../game/npc';
	import {ALL_UNIT_TYPES, UNIT_STATS, type UnitType} from '../game/army';
	import {calculateDerived} from '../game/attributes';
	import {loadTrack, playTrack} from '../game/audio';
	import {color, btnProps, messageStyle, mutedStyle} from '../ui/theme';

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

	let {player, gameState, settlement, seed, mode, fightContext, onExit, onTrade}: Props = $props();

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

	const isUrban = $derived(mode === 'city' || mode === 'village');

	const locationName = $derived(
		isUrban && settlement
			? settlement.name
			: 'The Wilds',
	);

	// ── Helpers ──────────────────────────────────────────────────

	/** Compute player melee damage from RPG attributes. */
	function playerDamage(): number {
		const derived = calculateDerived(player.attributes);
		const base = 10;
		return Math.floor(base * derived.physDamageMult);
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
		const rng = seededRng(cfgSeed + 7);

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

		// TEST: 100% citizens, 0% guards. Normal is ~85% citizens / ~15% guards.
		const npcDistribution: Array<{type: NPCType; weight: number}> = [
			{type: NPCType.Peasant, weight: 0.55},
			{type: NPCType.Merchant, weight: 0.20},
			{type: NPCType.Woodcutter, weight: 0.20},
			{type: NPCType.Witch, weight: 0.05},
			{type: NPCType.Guard, weight: 0},
			{type: NPCType.Sorceress, weight: 0},
		];

		// Guard types that fight instead of fleeing
		const guardTypes = new Set([NPCType.Guard, NPCType.Sorceress]);

		for (let i = 0; i < s.population; i++) {
			const spot = findRoadNearHouses(mapData.tileGrid, mapData.width, mapData.height, rng);
			if (spot) {
				// Pick NPC type from weighted distribution
				let roll = rng();
				let nType = NPCType.Peasant;
				for (const entry of npcDistribution) {
					roll -= entry.weight;
					if (roll <= 0) {
						nType = entry.type;
						break;
					}
				}

				const isGuard = guardTypes.has(nType);
				const def = NPC_TYPE_DEFS[nType] ?? NPC_TYPE_DEFS[NPCType.Peasant];
				const combat = def.combat;
				const npcLevel = def.baseLevel + Math.floor(rng() * 3);
				const hpScale = 1 + (npcLevel - 1) * 0.15;
				const hp = Math.floor(combat.hp * hpScale);

				entities.push(makeEntity(nextId, {
					kind: 'npc', x: spot.x, y: spot.y, radius: 0.5, solid: true,
					label: def.names[Math.floor(rng() * def.names.length)],
					color: `hsl(${Math.floor(rng() * 360)}, 40%, 55%)`,
					ai: isGuard ? 'combat' : 'flee',
					aiTimer: rng() * 3,
					spriteIndex: i % citizenSheet.count,
					// Combat stats from NPC template
					hp, maxHp: hp,
					damage: Math.floor(combat.damage * hpScale),
					speed: combat.speed,
					attackRange: combat.attackRange,
					cooldown: combat.cooldown,
					factionId: cityFaction,
					npcType: nType,
				}));
			}
		}

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
		const rng = seededRng(natureSeed + 13);
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
		const banditDef = NPC_TYPE_DEFS[NPCType.Bandit];
		const banditCount = 3 + Math.floor(rng() * 5);
		for (let i = 0; i < banditCount; i++) {
			const spot = findWalkable(traversability, rng, mapData.spawnX + 80, mapData.spawnY, 150);
			if (spot) {
				const bc = banditDef.combat;
				const lvl = banditDef.baseLevel + Math.floor(rng() * 3);
				const scale = 1 + (lvl - 1) * 0.15;
				const hp = Math.floor(bc.hp * scale);
				entities.push(makeEntity(nextId, {
					kind: 'npc', x: spot.x, y: spot.y, radius: 1.2, solid: true,
					label: banditDef.names[Math.floor(rng() * banditDef.names.length)],
					color: '#cc4444',
					hp, maxHp: hp,
					damage: Math.floor(bc.damage * scale),
					speed: bc.speed,
					attackRange: bc.attackRange,
					cooldown: bc.cooldown,
					factionId: 'cults',
					npcType: NPCType.Bandit,
					ai: 'wander', aiTimer: rng() * 3,
				}));
			}
		}

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
				0: '#4488ff', // Swordsman — blue
				1: '#44cc44', // Archer — green
				2: '#aaaa44', // Spearman — olive
				3: '#cc8844', // Horseman — brown
			};

			const enemyColors: Record<number, string> = {
				0: '#cc4444', // Swordsman — red
				1: '#cc6644', // Archer — orange-red
				2: '#884444', // Spearman — dark red
				3: '#cc4488', // Horseman — magenta-red
			};

			// Spawn player's army near the player
			for (const ut of ALL_UNIT_TYPES) {
				const qty = fightContext.playerArmy[ut] ?? 0;
				const stats = UNIT_STATS[ut];
				for (let i = 0; i < qty; i++) {
					const spot = findWalkable(
						traversability, rng,
						mapData.spawnX, mapData.spawnY, 40,
					);
					if (spot) {
						entities.push(makeEntity(nextId, {
							kind: 'npc', x: spot.x, y: spot.y,
							radius: 1.0, solid: true,
							label: stats.label,
							color: unitColors[ut as number] ?? '#4488ff',
							hp: stats.hp, maxHp: stats.hp,
							damage: stats.damage,
							speed: stats.speed,
							attackRange: stats.attackRange,
							cooldown: stats.cooldown,
							unitType: ut as number,
							factionId: 'player_army',
							ai: 'combat', aiTimer: 0,
						}));
					}
				}
			}

			// Spawn enemy army further away
			const enemyCx = mapData.spawnX + 160;
			const enemyCy = mapData.spawnY;
			for (const ut of ALL_UNIT_TYPES) {
				const qty = fightContext.enemyArmy[ut] ?? 0;
				const stats = UNIT_STATS[ut];
				for (let i = 0; i < qty; i++) {
					const spot = findWalkable(
						traversability, rng,
						enemyCx, enemyCy, 40,
					);
					if (spot) {
						entities.push(makeEntity(nextId, {
							kind: 'npc', x: spot.x, y: spot.y,
							radius: 1.0, solid: true,
							label: `${fightContext.enemyName} ${stats.label}`,
							color: enemyColors[ut as number] ?? '#cc4444',
							hp: stats.hp, maxHp: stats.hp,
							damage: stats.damage,
							speed: stats.speed,
							attackRange: stats.attackRange,
							cooldown: stats.cooldown,
							unitType: ut as number,
							factionId: fightContext.enemyFactionId,
							ai: 'combat', aiTimer: 0,
						}));
					}
				}
			}
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
			const eFac = fightContext.enemyFactionId;
			fc.reputation[eFac] = -100;
			fc.factions.player_army ??= {relations: {}};
			fc.factions.player_army.relations[eFac] = -100;
			fc.factions[eFac] ??= {relations: {}};
			fc.factions[eFac].relations.player_army = -100;
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
		void loadTrack('subworld', '/assets/sound/subworld.mp3').then(() => {
			void playTrack('subworld');
		});

		const configPromise = isUrban
			? buildCityConfig()
			: buildNatureConfig();

		let cancelled = false;

		configPromise.then(config => {
			if (cancelled) return;

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

				if (engine) {
					engine.inputDir = {
						x: (pressed.has('ArrowRight') ? 1 : 0)
							- (pressed.has('ArrowLeft') ? 1 : 0),
						y: (pressed.has('ArrowDown') ? 1 : 0)
							- (pressed.has('ArrowUp') ? 1 : 0),
					};
					engine.attackHeld = pressed.has('a') || pressed.has('A');

					engine.tick(dt);

					// Sync player HP back to macroworld state
					if (engine.player.hp !== undefined) {
						player.combatStats.currentHp = Math.max(0, engine.player.hp);
					}

					const action = engine.consumeAction();
					if (action) handleAction(action);

					// Count hostiles vs friendlies
					friendlyCount = engine.entities.filter(ent =>
						ent !== engine!.player
					&& ent.kind === 'npc'
					&& (ent.hp ?? 0) > 0
					&& !engine!.isHostileToPlayer(ent),
				).length;
				enemyCount = engine.entities.filter(ent =>
					ent !== engine!.player
					&& ent.kind === 'npc'
						&& (ent.hp ?? 0) > 0
						&& engine!.isHostileToPlayer(ent),
					).length;

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
	<div class="relative flex-1" onwheel={handleWheel}>
		<canvas bind:this={canvas} class="h-full w-full" style="image-rendering: pixelated;"></canvas>

		{#if loading}
			<div class="absolute inset-0 flex items-center justify-center bg-black/80">
				<span class="font-sans text-sm" style={mutedStyle}>Generating world...</span>
			</div>
		{/if}

		<div class="pointer-events-none absolute bottom-4 left-4 rounded bg-black/60 px-3 py-2 font-sans text-xs" style={mutedStyle}>
			Arrows to move · A to attack · Walk into zones to interact
		</div>

		{#if message}
			<div class="absolute bottom-4 left-1/2 -translate-x-1/2 rounded border px-4 py-2 text-center font-sans text-sm shadow-lg" style={messageStyle}>
				{message}
			</div>
		{/if}
	</div>
</div>

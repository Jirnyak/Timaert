<script lang="ts">
	import {onMount} from 'svelte';
	import type {PlayerState, Settlement} from '../game/state';
	import {SubworldEngine, SubworldRenderer, createSettlementSubworld, createNatureSubworld} from '../game/subworld';
	import type {ZoneAction} from '../game/subworld';
	import {loadTrack, playTrack} from '../game/audio';
	import {color, btnProps, messageStyle, mutedStyle} from '../ui/theme';

	type Props = {
		player: PlayerState;
		settlement?: Settlement;
		seed: number;
		mode: 'city' | 'nature';
		onExit: () => void;
		onTrade: () => void;
	};

	let {player, settlement, seed, mode, onExit, onTrade}: Props = $props();

	let canvas: HTMLCanvasElement;
	let message = $state('');
	let messageTimer = 0;
	let engine: SubworldEngine | undefined;
	let renderer: SubworldRenderer | undefined;
	let animFrame = 0;
	let loading = $state(true);
	let zoom = $state(1);

	// Zoom limits
	const ZOOM_MIN = 0.25;
	const ZOOM_MAX = 4;
	const ZOOM_STEP = 1.15;

	// Input state
	const pressed = new Set<string>();

	const locationName = $derived(
		mode === 'city' && settlement
			? settlement.name
			: 'The Wilds',
	);

	onMount(() => {
		// Load and play subworld music
		void loadTrack('subworld', '/assets/sound/subworld.mp3').then(() => {
			void playTrack('subworld');
		});

		const configPromise = mode === 'city' && settlement
			? createSettlementSubworld({
				seed: seed + settlement.id * 123,
				name: settlement.name,
				population: settlement.population,
				economy: settlement.economy,
				mood: settlement.mood,
				characterData: player.characterData,
			})
			: createNatureSubworld({
				seed,
				name: 'The Wilds',
				characterData: player.characterData,
			});

		let cancelled = false;

		configPromise.then(config => {
			if (cancelled) {
				return;
			}

			loading = false;
			engine = new SubworldEngine(config);
			renderer = new SubworldRenderer(canvas);

			let lastTime = performance.now();

			function frame(now: number) {
				const dt = (now - lastTime) / 1000; // seconds
				lastTime = now;

				if (engine) {
					// Feed input
					engine.inputDir = {
						x: (pressed.has('d') || pressed.has('ArrowRight') ? 1 : 0)
							- (pressed.has('a') || pressed.has('ArrowLeft') ? 1 : 0),
						y: (pressed.has('s') || pressed.has('ArrowDown') ? 1 : 0)
							- (pressed.has('w') || pressed.has('ArrowUp') ? 1 : 0),
					};

					engine.tick(dt);

					// Check zone triggers
					const action = engine.consumeAction();
					if (action) {
						handleAction(action);
					}

					// Render
					if (renderer) {
						const effectiveScale = (engine.config.scale || 40) * zoom;
						renderer.render(engine.config, engine.player.x, engine.player.y, effectiveScale);
					}
				}

				// Fade message
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
			// Restore overworld music
			void playTrack('explore');
		};
	});

	function handleAction(action: ZoneAction) {
		switch (action.type) {
			case 'exit': {
				onExit();
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

			default:
				break;
		}
	}

	function showMessage(text: string) {
		message = text;
		messageTimer = 3;
	}

	function handleKeyDown(event: KeyboardEvent) {
		if (event.key === 'Escape') {
			onExit();
			return;
		}

		pressed.add(event.key);
	}

	function handleKeyUp(event: KeyboardEvent) {
		pressed.delete(event.key);
	}

	function handleWheel(event: WheelEvent) {
		event.preventDefault();
		if (event.deltaY < 0) {
			zoom = Math.min(ZOOM_MAX, zoom * ZOOM_STEP);
		} else {
			zoom = Math.max(ZOOM_MIN, zoom / ZOOM_STEP);
		}
	}
</script>

<svelte:window onkeydown={handleKeyDown} onkeyup={handleKeyUp} />

<div class="absolute inset-0 z-[100] flex flex-col" style="background: #1a1a1a;">
	<!-- HUD top bar -->
	<div class="flex items-center justify-between bg-black/80 px-4 py-2 font-sans text-sm">
		<div class="flex items-center gap-4">
			<span class="font-bold uppercase tracking-wider" style="color: {color.accent};">{locationName}</span>
			<span style="color: {color.hp};">HP: {player.combatStats.currentHp}/{player.combatStats.maxHp}</span>
			<span style="color: {color.mp};">MP: {player.combatStats.currentMp}/{player.combatStats.maxMp}</span>
			<span style="color: {color.sp};">SP: {Math.floor(player.combatStats.currentSp)}/{player.combatStats.maxSp}</span>
			<span class="text-yellow-400">Gold: {player.gold}</span>
		</div>
		<button
			onclick={onExit}
			class="rounded border-2 px-3 py-1 text-xs font-bold uppercase tracking-wide transition"
			{...btnProps('close')}
		>Leave [Esc]</button>
	</div>

	<!-- Canvas area -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div class="relative flex-1" onwheel={handleWheel}>
		<canvas
			bind:this={canvas}
			class="h-full w-full"
			style="image-rendering: pixelated;"
		></canvas>

		<!-- Loading overlay -->
		{#if loading}
			<div class="absolute inset-0 flex items-center justify-center bg-black/80">
				<span class="font-sans text-sm" style={mutedStyle}>Generating world...</span>
			</div>
		{/if}

		<!-- Controls hint -->
		<div class="pointer-events-none absolute bottom-4 left-4 rounded bg-black/60 px-3 py-2 font-sans text-xs" style={mutedStyle}>
			WASD / Arrows to move · Walk into zones to interact
		</div>

		<!-- Message toast -->
		{#if message}
			<div class="absolute bottom-4 left-1/2 -translate-x-1/2 rounded border px-4 py-2 text-center font-sans text-sm shadow-lg" style={messageStyle}>
				{message}
			</div>
		{/if}
	</div>
</div>

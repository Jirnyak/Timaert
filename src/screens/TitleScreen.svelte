<script lang="ts">
	import {onMount} from 'svelte';
	import TitleBackground from './TitleBackground.svelte';
	import {loadTrack, playTrack, toggleMute, isMuted, resumeOnInteraction} from '../game/audio';

	type Props = {
		onNewGame: () => void;
		onSandbox: () => void;
		onLoad: () => void;
	};

	let {onNewGame, onSandbox, onLoad}: Props = $props();

	let audioMuted = $state(isMuted());

	onMount(() => {
		void loadTrack('explore', '/assets/sound/15-dungeon-suno.mp3');
	});

	function handleClick(action: () => void) {
		// User gesture — safe to start audio now
		resumeOnInteraction();
		void playTrack('explore');
		action();
	}

	function handleMuteToggle() {
		resumeOnInteraction();
		audioMuted = toggleMute();
	}

	// RPG Awesome icon codepoints
	const RA_FLOWER = '\uE9CD';
	const RA_TOWER = '\uEAD2';
	const RA_LOAD = '\uEA34';
	const RA_GEARS = '\uE9DD';
</script>

<div class="relative h-full w-full">
	<TitleBackground />

	<div class="absolute inset-0 flex flex-col items-center justify-center">
		<!-- Logo -->
		<img
			src="/assets/sprites/logo.png"
			alt="Samosbor"
			class="mb-8 h-24 object-contain drop-shadow-lg"
			style="filter: drop-shadow(0 2px 8px rgba(0,0,0,0.8));"
		/>

		<div class="flex w-96 flex-col gap-3">
			<button
				onclick={() => handleClick(onNewGame)}
				class="flex items-center gap-4 rounded border-2 font-sans text-lg font-bold text-amber-950 shadow-lg transition"
				style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #8b6f47; box-shadow: 0 3px 6px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.3);"
				onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #f0dcc5, #dcc7a7)'}
				onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e8d4b8, #d4bf9f)'}
			>
				<span class="ra-icon text-2xl text-amber-800" style="text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_FLOWER}</span>
				<span class="px-2 py-2" style="text-shadow: 0 1px 2px rgba(255,255,255,0.5);">New Game</span>
			</button>

			<button
				onclick={() => handleClick(onSandbox)}
				class="flex items-center gap-4 rounded border-2 font-sans text-lg font-bold text-amber-950 shadow-lg transition"
				style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #8b6f47; box-shadow: 0 3px 6px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.3);"
				onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #f0dcc5, #dcc7a7)'}
				onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e8d4b8, #d4bf9f)'}
			>
				<span class="ra-icon text-2xl text-orange-800" style="text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_GEARS}</span>
				<span class="px-2 py-2" style="text-shadow: 0 1px 2px rgba(255,255,255,0.5);">Sandbox</span>
			</button>

			<button
				disabled
				class="flex cursor-not-allowed items-center gap-4 rounded border-2 font-sans text-lg font-bold text-stone-600 shadow-lg"
				style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b5f47; opacity: 0.6;"
			>
				<span class="ra-icon text-2xl text-stone-700">{RA_TOWER}</span>
				<span class="px-2 py-2">Labyrinth</span>
			</button>

			<button
				onclick={() => handleClick(onLoad)}
				class="flex items-center gap-4 rounded border-2 font-sans text-lg font-bold text-amber-950 shadow-lg transition"
				style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #8b6f47; box-shadow: 0 3px 6px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.3);"
				onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #f0dcc5, #dcc7a7)'}
				onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e8d4b8, #d4bf9f)'}
			>
				<span class="ra-icon text-2xl text-yellow-800" style="text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_LOAD}</span>
				<span class="px-2 py-2" style="text-shadow: 0 1px 2px rgba(255,255,255,0.5);">Load</span>
			</button>
		</div>

		<!-- Mute toggle -->
		<button
			onclick={handleMuteToggle}
			class="mt-6 rounded border-2 font-sans text-sm transition"
			style="background: linear-gradient(to bottom, #c8b89f, #b0a080); border-color: #7a6847; color: #3d2817; padding: 8px 16px; box-shadow: 0 2px 4px rgba(0,0,0,0.4);"
			onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c0b090)'}
			onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b0a080)'}
		>{audioMuted ? 'Unmute Music' : 'Mute Music'}</button>
	</div>
</div>

<style>
	.ra-icon {
		font-family: 'RPG Awesome', sans-serif;
	}
</style>

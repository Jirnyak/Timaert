<script lang="ts">
	import {onMount} from 'svelte';
	import TitleBackground from './TitleBackground.svelte';
	import {loadBgm, playBgm, toggleMute, isMuted, resumeOnInteraction} from '../game/audio';

	type Props = {
		onNewGame: () => void;
		onSandbox: () => void;
		onLoad: () => void;
	};

	let {onNewGame, onSandbox, onLoad}: Props = $props();

	let audioMuted = $state(isMuted());

	onMount(() => {
		void loadBgm('/assets/sound/15-dungeon-suno.mp3');
	});

	function handleClick(action: () => void) {
		// User gesture — safe to start audio now
		resumeOnInteraction();
		void playBgm();
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
		/>

		<div class="flex w-96 flex-col gap-3">
			<button
				onclick={() => handleClick(onNewGame)}
				class="flex items-center gap-4 rounded border border-cyan-700/50 bg-slate-800/85 px-6 py-4 font-sans text-lg font-bold text-white shadow-lg transition hover:bg-slate-700/90 hover:border-cyan-500/70"
			>
				<span class="ra-icon text-2xl text-cyan-400">{RA_FLOWER}</span>
				New Game
			</button>

			<button
				onclick={() => handleClick(onSandbox)}
				class="flex items-center gap-4 rounded border border-cyan-700/50 bg-slate-800/85 px-6 py-4 font-sans text-lg font-bold text-white shadow-lg transition hover:bg-slate-700/90 hover:border-cyan-500/70"
			>
				<span class="ra-icon text-2xl text-amber-400">{RA_GEARS}</span>
				Sandbox
			</button>

			<button
				disabled
				class="flex cursor-not-allowed items-center gap-4 rounded border border-gray-700/50 bg-slate-800/60 px-6 py-4 font-sans text-lg font-bold text-gray-500 shadow-lg"
			>
				<span class="ra-icon text-2xl text-gray-600">{RA_TOWER}</span>
				Labyrinth
			</button>

			<button
				onclick={() => handleClick(onLoad)}
				class="flex items-center gap-4 rounded border border-yellow-700/50 bg-slate-800/85 px-6 py-4 font-sans text-lg font-bold text-white shadow-lg transition hover:bg-slate-700/90 hover:border-yellow-500/70"
			>
				<span class="ra-icon text-2xl text-yellow-400">{RA_LOAD}</span>
				Load
			</button>
		</div>

		<!-- Mute toggle -->
		<button
			onclick={handleMuteToggle}
			class="mt-6 rounded border border-gray-700/50 bg-slate-800/60 px-4 py-2 font-sans text-sm text-gray-400 transition hover:bg-slate-700/60"
		>{audioMuted ? 'Unmute Music' : 'Mute Music'}</button>
	</div>
</div>

<style>
	.ra-icon {
		font-family: 'RPG Awesome', sans-serif;
	}
</style>

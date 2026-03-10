<script lang="ts">
	import {onMount} from 'svelte';
	import {
		loadTrack, playTrack, toggleMute, isMuted, resumeOnInteraction,
	} from '../game/audio';
	import {
		color, btnStyle, btnHover, btnOut,
	} from '../ui/theme';
	import TitleBackground from './TitleBackground.svelte';

	type Props = {
		onNewGame: () => void;
		onSandbox: () => void;
		onLoad: () => void;
	};

	const {onNewGame, onSandbox, onLoad}: Props = $props();

	let audioMuted = $state(isMuted());

	onMount(() => {
		loadTrack('explore', '/assets/sound/15-dungeon-suno.mp3');
	});

	function handleClick(action: () => void) {
		// User gesture — safe to start audio now
		resumeOnInteraction();
		playTrack('explore');
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

	const titleBtnClass = 'flex items-center gap-4 rounded border-2 font-sans text-lg font-bold text-amber-950 shadow-lg transition';
	const titleExtra = 'box-shadow: 0 3px 6px rgba(0,0,0,0.5), inset 0 1px 0 rgba(255,255,255,0.3);';
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
				class={titleBtnClass}
				style="{btnStyle('title')} {titleExtra}"
				onmouseover={btnHover('title')}
				onmouseout={btnOut('title')}
				onfocus={btnHover('title')}
				onblur={btnOut('title')}
			>
				<span class="ra-icon text-2xl text-amber-800" style="text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_FLOWER}</span>
				<span class="px-2 py-2" style="text-shadow: {color.headingShadow};">New Game</span>
			</button>

			<button
				onclick={() => handleClick(onSandbox)}
				class={titleBtnClass}
				style="{btnStyle('title')} {titleExtra}"
				onmouseover={btnHover('title')}
				onmouseout={btnOut('title')}
				onfocus={btnHover('title')}
				onblur={btnOut('title')}
			>
				<span class="ra-icon text-2xl text-orange-800" style="text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_GEARS}</span>
				<span class="px-2 py-2" style="text-shadow: {color.headingShadow};">Sandbox</span>
			</button>

			<button
				disabled
				class="flex cursor-not-allowed items-center gap-4 rounded border-2 font-sans text-lg font-bold text-stone-600 shadow-lg"
				style="background: {color.messageBg}; border-color: #6b5f47; opacity: 0.6;"
			>
				<span class="ra-icon text-2xl text-stone-700">{RA_TOWER}</span>
				<span class="px-2 py-2">Labyrinth</span>
			</button>

			<button
				onclick={() => handleClick(onLoad)}
				class={titleBtnClass}
				style="{btnStyle('title')} {titleExtra}"
				onmouseover={btnHover('title')}
				onmouseout={btnOut('title')}
				onfocus={btnHover('title')}
				onblur={btnOut('title')}
			>
				<span class="ra-icon text-2xl text-yellow-800" style="text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_LOAD}</span>
				<span class="px-2 py-2" style="text-shadow: {color.headingShadow};">Load</span>
			</button>
		</div>

		<!-- Mute toggle -->
		<button
			onclick={handleMuteToggle}
			class="mt-6 rounded border-2 font-sans text-sm transition"
			style="{btnStyle('menu')} padding: 8px 16px; box-shadow: 0 2px 4px rgba(0,0,0,0.4);"
			onmouseover={btnHover('menu')}
			onmouseout={btnOut('menu')}
			onfocus={btnHover('menu')}
			onblur={btnOut('menu')}
		>{audioMuted ? 'Unmute Music' : 'Mute Music'}</button>
	</div>
</div>

<style>
	.ra-icon {
		font-family: 'RPG Awesome', sans-serif;
	}
</style>

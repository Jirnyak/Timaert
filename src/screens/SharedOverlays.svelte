<script lang="ts">
	import type {PlayerState, GameState} from '../game/state';
	import type {SharedOverlayId} from './shared-overlays';
	import StatOverlay from './StatOverlay.svelte';
	import SpellOverlay from './SpellOverlay.svelte';

	type Props = {
		player: PlayerState;
		gameState: GameState;
		/** True when rendered inside the subworld (affects spell cost rules). */
		inMicro: boolean;
		/** Bindable: id of the currently-open shared overlay, or undefined. */
		open: SharedOverlayId | undefined;
	};

	let {
		player = $bindable(),
		gameState,
		inMicro,
		open = $bindable(),
	}: Props = $props();

	function close() {
		open = undefined;
	}
</script>

{#if open === 'inventory'}
	<StatOverlay
		bind:player
		deserterPool={gameState.deserterPool}
		onClose={close}
	/>
{/if}

{#if open === 'spells'}
	<SpellOverlay
		{player}
		spellBook={player.spellBook}
		{inMicro}
		onClose={close}
	/>
{/if}

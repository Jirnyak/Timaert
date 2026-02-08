<script lang="ts">
	import {type AppScreen, type GameState, createRandomGameState, loadGame} from './game/state';
	import TitleScreen from './screens/TitleScreen.svelte';
	import LoadScreen from './screens/LoadScreen.svelte';
	import SandboxSetup from './screens/SandboxSetup.svelte';
	import GameScreen from './screens/GameScreen.svelte';

	let screen: AppScreen = $state({type: 'title'});

	function onNewGame() {
		const state = createRandomGameState();
		screen = {type: 'game', state};
	}

	function onSandbox() {
		screen = {type: 'sandbox_setup'};
	}

	function onLoad() {
		screen = {type: 'load'};
	}

	function onLoadGame(key: string) {
		const state = loadGame(key);
		if (state) {
			screen = {type: 'game', state};
		}
	}

	function onStartSandbox(state: GameState) {
		screen = {type: 'game', state};
	}

	function onBackToTitle() {
		screen = {type: 'title'};
	}
</script>

<div class="h-screen w-screen overflow-hidden bg-gray-950">
	{#if screen.type === 'title'}
		<TitleScreen {onNewGame} {onSandbox} {onLoad} />
	{:else if screen.type === 'load'}
		<LoadScreen {onLoadGame} onBack={onBackToTitle} />
	{:else if screen.type === 'sandbox_setup'}
		<SandboxSetup onStart={onStartSandbox} onBack={onBackToTitle} />
	{:else if screen.type === 'game'}
		<GameScreen gameState={screen.state} {onBackToTitle} {onLoadGame} />
	{/if}
</div>

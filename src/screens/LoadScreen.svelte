<script lang="ts">
	import {listSaves, deleteSave} from '../game/state';

	type Props = {
		onLoadGame: (key: string) => void;
		onBack: () => void;
	};

	let {onLoadGame, onBack}: Props = $props();

	let saves = $state(listSaves());

	function handleDelete(key: string) {
		deleteSave(key);
		saves = listSaves();
	}
</script>

<div class="flex h-full w-full flex-col items-center justify-center bg-gray-950">
	<h1 class="mb-8 font-sans text-3xl font-bold text-white">Load Game</h1>

	{#if saves.length === 0}
		<p class="mb-8 font-sans text-gray-400">No saved games found.</p>
	{:else}
		<div class="mb-8 flex w-[500px] max-w-[90vw] flex-col gap-2 overflow-y-auto" style="max-height: 60vh">
			{#each saves as save (save.key)}
				<div class="flex items-center gap-2 rounded border border-gray-700 bg-gray-800 p-3">
					<div class="flex-1">
						<div class="font-sans text-sm font-bold text-white">{save.name}</div>
						<div class="font-sans text-xs text-gray-400">
							{new Date(save.savedAt).toLocaleString()}
						</div>
					</div>
					<button
						onclick={() => onLoadGame(save.key)}
						class="rounded bg-blue-700 px-4 py-1 font-sans text-sm text-white hover:bg-blue-600"
					>
						Load
					</button>
					<button
						onclick={() => handleDelete(save.key)}
						class="rounded bg-red-800 px-3 py-1 font-sans text-sm text-white hover:bg-red-700"
					>
						&#10005;
					</button>
				</div>
			{/each}
		</div>
	{/if}

	<button
		onclick={onBack}
		class="rounded border border-gray-600 bg-gray-800 px-8 py-3 font-sans text-lg text-white hover:bg-gray-700"
	>
		Back
	</button>
</div>

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

<div class="flex h-full w-full flex-col items-center justify-center" style="background: linear-gradient(to bottom, #2a1810, #1a0f08);">
	<h1 class="mb-8 font-sans text-3xl font-bold" style="color: #d4a574; text-shadow: 0 2px 4px rgba(0,0,0,0.5);">Load Game</h1>

	{#if saves.length === 0}
		<p class="mb-8 font-sans" style="color: #9a8a7a;">No saved games found.</p>
	{:else}
		<div class="mb-8 flex w-[500px] max-w-[90vw] flex-col gap-2 overflow-y-auto" style="max-height: 60vh">
			{#each saves as save (save.key)}
				<div class="flex items-center gap-2 rounded border-2 p-3" style="border-color: #8b6f47; background: linear-gradient(to bottom, #d4bf9f, #c4af8f);">
					<div class="flex-1">
						<div class="font-sans text-sm font-bold" style="color: #3d2817;">{save.name}</div>
						<div class="font-sans text-xs" style="color: #6a5a4a;">
							{new Date(save.savedAt).toLocaleString()}
						</div>
					</div>
					<button
						onclick={() => onLoadGame(save.key)}
						class="rounded border-2 px-4 py-1 font-sans text-sm transition"
						style="background: linear-gradient(to bottom, #8a9aaa, #6a7a8a); border-color: #5a6a7a; color: #f0e8d8;"
						onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #9aaaba, #7a8a9a)'}
						onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #8a9aaa, #6a7a8a)'}
					>
						Load
					</button>
					<button
						onclick={() => handleDelete(save.key)}
						class="rounded border-2 px-3 py-1 font-sans text-sm transition"
						style="background: linear-gradient(to bottom, #c86a6a, #a84a4a); border-color: #8a3a3a; color: #f0e8d8;"
						onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d87a7a, #b85a5a)'}
						onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c86a6a, #a84a4a)'}
					>
						&#10005;
					</button>
				</div>
			{/each}
		</div>
	{/if}

	<button
		onclick={onBack}
		class="rounded border-2 px-8 py-3 font-sans text-lg transition"
		style="background: linear-gradient(to bottom, #c8b89f, #b8a88f); border-color: #8b6f47; color: #3d2817;"
		onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c8b89f)'}
		onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b8a88f)'}
	>
		Back
	</button>
</div>

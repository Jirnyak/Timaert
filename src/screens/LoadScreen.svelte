<script lang="ts">
	import {listSaves, deleteSave} from '../game/state';
	import {color, btnProps} from '../ui/theme';

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

<div class="flex h-full w-full flex-col items-center justify-center" style="background: {color.pageGradient};">
	<h1 class="mb-8 font-sans text-3xl font-bold" style="color: {color.accent}; text-shadow: 0 2px 4px rgba(0,0,0,0.5);">Load Game</h1>

	{#if saves.length === 0}
		<p class="mb-8 font-sans" style="color: #9a8a7a;">No saved games found.</p>
	{:else}
		<div class="mb-8 flex w-[500px] max-w-[90vw] flex-col gap-2 overflow-y-auto" style="max-height: 60vh">
			{#each saves as save (save.key)}
				<div class="flex items-center gap-2 rounded border-2 p-3" style="border-color: {color.divider}; background: {color.cardBg};">
					<div class="flex-1">
						<div class="font-sans text-sm font-bold" style="color: {color.heading};">{save.name}</div>
						<div class="font-sans text-xs" style="color: {color.subtitle};">
							{new Date(save.savedAt).toLocaleString()}
						</div>
					</div>
					<button
						onclick={() => onLoadGame(save.key)}
						class="rounded border-2 px-4 py-1 font-sans text-sm transition"
						{...btnProps('action')}
					>
						Load
					</button>
					<button
						onclick={() => handleDelete(save.key)}
						class="rounded border-2 px-3 py-1 font-sans text-sm transition"
						{...btnProps('danger')}
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
		{...btnProps('secondary')}
	>
		Back
	</button>
</div>

<script lang="ts">
	import {listSaves} from '../game/state';

	type Props = {
		onResume: () => void;
		onSave: () => void;
		onLoad: (key: string) => void;
		onToTitle: () => void;
	};

	let {onResume, onSave, onLoad, onToTitle}: Props = $props();

	let showLoadList = $state(false);
	let saves = $state(listSaves());

	// RPG Awesome icon codepoints
	const RA_FORWARD = '\uE9D4';
	const RA_SAVE = '\uEA8D';
	const RA_LOAD = '\uEA34';
	const RA_CASTLE = '\uE95D';

	function handleSave() {
		onSave();
		saves = listSaves();
	}

	function toggleLoad() {
		showLoadList = !showLoadList;
		if (showLoadList) {
			saves = listSaves();
		}
	}
</script>

<div class="absolute inset-0 flex items-center justify-center bg-black/60">
	<div class="flex w-96 flex-col gap-3">
		<h2 class="mb-2 text-center font-sans text-3xl font-black tracking-wide text-white">PAUSED</h2>

		<button
			onclick={onResume}
			class="flex items-center gap-4 rounded border border-cyan-600/50 bg-slate-800/90 px-6 py-4 font-sans text-lg font-bold text-white transition hover:bg-slate-700/90"
		>
			<span class="ra-icon text-2xl text-cyan-400">{RA_FORWARD}</span>
			Resume
		</button>

		<button
			onclick={handleSave}
			class="flex items-center gap-4 rounded border border-cyan-600/50 bg-slate-800/90 px-6 py-4 font-sans text-lg font-bold text-white transition hover:bg-slate-700/90"
		>
			<span class="ra-icon text-2xl text-green-400">{RA_SAVE}</span>
			Save
		</button>

		<button
			onclick={toggleLoad}
			class="flex items-center gap-4 rounded border border-yellow-600/50 bg-slate-800/90 px-6 py-4 font-sans text-lg font-bold text-white transition hover:bg-slate-700/90"
		>
			<span class="ra-icon text-2xl text-yellow-400">{RA_LOAD}</span>
			Load
		</button>

		{#if showLoadList}
			<div class="max-h-48 overflow-y-auto rounded border border-gray-700 bg-gray-900 p-2">
				{#if saves.length === 0}
					<p class="py-2 text-center font-sans text-sm text-gray-500">No saves found</p>
				{:else}
					{#each saves as save (save.key)}
						<button
							onclick={() => onLoad(save.key)}
							class="mb-1 block w-full rounded bg-gray-800 p-2 text-left font-sans text-sm text-white hover:bg-gray-700"
						>
							<span class="font-bold">{save.name}</span>
							<span class="ml-2 text-xs text-gray-400">{new Date(save.savedAt).toLocaleString()}</span>
						</button>
					{/each}
				{/if}
			</div>
		{/if}

		<button
			onclick={onToTitle}
			class="flex items-center gap-4 rounded border border-cyan-600/50 bg-slate-800/90 px-6 py-4 font-sans text-lg font-bold text-white transition hover:bg-slate-700/90"
		>
			<span class="ra-icon text-2xl text-amber-400">{RA_CASTLE}</span>
			To main menu
		</button>
	</div>
</div>

<style>
	.ra-icon {
		font-family: 'RPG Awesome', sans-serif;
	}
</style>

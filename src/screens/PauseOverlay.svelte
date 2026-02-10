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

<div class="absolute inset-0 flex items-center justify-center" style="background: rgba(20, 10, 5, 0.85);">
	<div class="flex w-96 flex-col gap-3 rounded-lg border-4 p-6" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3);">
		<h2 class="mb-2 text-center font-sans text-3xl font-black tracking-wide" style="color: #3d2817; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">PAUSED</h2>

		<button
			onclick={onResume}
			class="flex items-center gap-4 rounded border-2 font-sans text-lg font-bold transition"
			style="background: linear-gradient(to bottom, #c8b89f, #b0a080); border-color: #6b4f3a; color: #3d2817; padding: 12px 16px; box-shadow: 0 2px 4px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.2); text-shadow: 0 1px 1px rgba(255,255,255,0.3);"
			onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c0b090)'}
			onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b0a080)'}
		>
			<span class="ra-icon text-2xl" style="color: #5a8a5a; text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_FORWARD}</span>
			Resume
		</button>

		<button
			onclick={handleSave}
			class="flex items-center gap-4 rounded border-2 font-sans text-lg font-bold transition"
			style="background: linear-gradient(to bottom, #c8b89f, #b0a080); border-color: #6b4f3a; color: #3d2817; padding: 12px 16px; box-shadow: 0 2px 4px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.2); text-shadow: 0 1px 1px rgba(255,255,255,0.3);"
			onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c0b090)'}
			onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b0a080)'}
		>
			<span class="ra-icon text-2xl" style="color: #4a7c4a; text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_SAVE}</span>
			Save
		</button>

		<button
			onclick={toggleLoad}
			class="flex items-center gap-4 rounded border-2 font-sans text-lg font-bold transition"
			style="background: linear-gradient(to bottom, #c8b89f, #b0a080); border-color: #6b4f3a; color: #3d2817; padding: 12px 16px; box-shadow: 0 2px 4px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.2); text-shadow: 0 1px 1px rgba(255,255,255,0.3);"
			onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c0b090)'}
			onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b0a080)'}
		>
			<span class="ra-icon text-2xl" style="color: #b8935a; text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_LOAD}</span>
			Load
		</button>

		{#if showLoadList}
			<div class="max-h-48 overflow-y-auto rounded border-2 p-2" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #5c422e;">
				{#if saves.length === 0}
					<p class="py-2 text-center font-sans text-sm" style="color: #5a4a3a;">No saves found</p>
				{:else}
					{#each saves as save (save.key)}
						<button
							onclick={() => onLoad(save.key)}
							class="mb-1 block w-full rounded border p-2 text-left font-sans text-sm transition"
							style="background: linear-gradient(to bottom, #d4bf9f, #c4af8f); border-color: #8b6f47; color: #3d2817;"
							onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #e4cfaf, #d4bf9f)'}
							onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d4bf9f, #c4af8f)'}
						>
							<span class="font-bold">{save.name}</span>
							<span class="ml-2 text-xs" style="color: #6b5a4a;">{new Date(save.savedAt).toLocaleString()}</span>
						</button>
					{/each}
				{/if}
			</div>
		{/if}

		<button
			onclick={onToTitle}
			class="flex items-center gap-4 rounded border-2 font-sans text-lg font-bold transition"
			style="background: linear-gradient(to bottom, #c8b89f, #b0a080); border-color: #6b4f3a; color: #3d2817; padding: 12px 16px; box-shadow: 0 2px 4px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.2); text-shadow: 0 1px 1px rgba(255,255,255,0.3);"
			onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c0b090)'}
			onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b0a080)'}
		>
			<span class="ra-icon text-2xl" style="color: #b8935a; text-shadow: 0 1px 2px rgba(0,0,0,0.3);">{RA_CASTLE}</span>
			To main menu
		</button>
	</div>
</div>

<style>
	.ra-icon {
		font-family: 'RPG Awesome', sans-serif;
	}
</style>

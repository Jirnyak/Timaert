<script lang="ts">
	import type {PlayerState} from '../game/state';
	import type {RandomEvent, EventResult} from '../game/events';

	type Props = {
		player: PlayerState;
		event: RandomEvent;
		onClose: () => void;
		onBattle: (enemyName: string, enemyType: number, enemyLevel: number) => void;
	};

	let {player = $bindable(), event, onClose, onBattle}: Props = $props();

	let result = $state<EventResult | undefined>(undefined);

	function choose(index: number) {
		const choice = event.choices[index];
		result = choice.effect(player);

		if (result.startBattle) {
			const {enemyName, enemyType, enemyLevel} = result.startBattle;
			onBattle(enemyName, enemyType, enemyLevel);
		}
	}
</script>

<div class="absolute inset-0 flex items-center justify-center bg-black/85">
	<div class="flex h-[380px] w-[540px] flex-col rounded-lg border-2 border-amber-900/60 bg-gray-950/95 font-sans shadow-2xl">
		<!-- Title -->
		<div class="border-b border-gray-800 px-5 py-3">
			<h2 class="text-lg font-black text-amber-400">{event.title}</h2>
		</div>

		<!-- Description -->
		<div class="border-b border-gray-800 px-5 py-3">
			<p class="text-sm leading-relaxed text-gray-300">{event.description}</p>
		</div>

		<!-- Choices / result (fixed area) -->
		<div class="flex flex-1 flex-col justify-center gap-2 p-4">
			{#if !result}
				{#each event.choices as choice, i}
					<button
						onclick={() => choose(i)}
						class="rounded border border-cyan-800 bg-gray-800/80 px-4 py-3 text-left text-sm font-bold text-white hover:bg-cyan-900/40"
					>{choice.label}</button>
				{/each}
			{:else}
				<div class="mb-3 rounded bg-gray-800 px-4 py-3 text-sm text-cyan-300">{result.message}</div>
				{#if !result.startBattle}
					<button
						onclick={onClose}
						class="rounded border border-cyan-800 bg-gray-800/80 px-4 py-3 text-sm font-bold text-white hover:bg-cyan-900/40"
					>Continue</button>
				{/if}
			{/if}
		</div>
	</div>
</div>

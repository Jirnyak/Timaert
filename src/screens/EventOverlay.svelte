<script lang="ts">
	import type {PlayerState} from '../game/state';
	import type {RandomEvent, EventResult} from '../game/events';

	type Props = {
		player: PlayerState;
		event: RandomEvent;
		currentDay: number;
		onClose: () => void;
		onBattle: (enemyName: string, enemyType: number, enemyLevel: number) => void;
	};

	let {player = $bindable(), event, currentDay, onClose, onBattle}: Props = $props();

	let result = $state<EventResult | undefined>(undefined);

	function choose(index: number) {
		const choice = event.choices[index];
		result = choice.effect(player);
		
		player.eventLog.push({
			type: result.startBattle ? 'combat' : 'world',
			day: currentDay,
			message: `Event: ${event.title} - ${choice.label}. Result: ${result.message}`
		});

		if (result.startBattle) {
			const {enemyName, enemyType, enemyLevel} = result.startBattle;
			onBattle(enemyName, enemyType, enemyLevel);
		}
	}
</script>

<div class="absolute inset-0 flex items-center justify-center" style="background: rgba(20, 10, 5, 0.9);">
	<div class="flex h-[380px] w-[540px] flex-col rounded-lg border-4 font-sans" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3);">
		<!-- Title -->
		<div class="border-b px-5 py-3" style="border-color: #8b6f47;">
			<h2 class="text-lg font-black" style="color: #8b6f3a; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">{event.title}</h2>
		</div>

		<!-- Description -->
		<div class="border-b px-5 py-3" style="border-color: #8b6f47;">
			<p class="text-sm leading-relaxed" style="color: #5a4a3a;">{event.description}</p>
		</div>

		<!-- Choices / result (fixed area) -->
		<div class="flex flex-1 flex-col justify-center gap-2 p-4">
			{#if !result}
				{#each event.choices as choice, i}
					<button
						onclick={() => choose(i)}
						class="rounded border-2 px-4 py-3 text-left text-sm font-bold transition"
						style="background: linear-gradient(to bottom, #c8b89f, #b8a88f); border-color: #8b6f47; color: #3d2817;"
						onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c8b89f)'}
						onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b8a88f)'}
					>{choice.label}</button>
				{/each}
			{:else}
				<div class="mb-3 rounded border px-4 py-3 text-sm" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;">{result.message}</div>
				{#if !result.startBattle}
					<button
						onclick={onClose}
						class="rounded border-2 px-4 py-3 text-sm font-bold transition"
						style="background: linear-gradient(to bottom, #c8b89f, #b8a88f); border-color: #8b6f47; color: #3d2817;"
						onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c8b89f)'}
						onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b8a88f)'}
					>Continue</button>
				{/if}
			{/if}
		</div>
	</div>
</div>

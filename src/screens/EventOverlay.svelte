<script lang="ts">
	import type {PlayerState} from '../game/state';
	import type {ShowDialogEvent, GameEvent, BattleStartEvent} from '../game/event-types';
	import {EventTag} from '../game/event-types';

	type Props = {
		player: PlayerState;
		dialog: ShowDialogEvent;
		currentDay: number;
		onClose: () => void;
		onBattle: (enemyName: string, enemyType: number, enemyLevel: number) => void;
		onEffects: (effects: GameEvent[]) => void;
	};

	let {player = $bindable(), dialog, currentDay, onClose, onBattle, onEffects}: Props = $props();

	let resultMessage = $state<string | undefined>(undefined);

	function choose(index: number) {
		const choice = dialog.choices[index];

		// Check gold requirements before applying
		if (choice.effects) {
			const goldCost = choice.effects.find(
				e => e.tag === EventTag.PlayerGoldChange && (e as any).delta < 0,
			);
			if (goldCost && player.gold < Math.abs((goldCost as any).delta)) {
				resultMessage = 'Not enough gold!';
				return;
			}
		}

		// Check for battle in effects
		const battleEffect = choice.effects?.find(e => e.tag === EventTag.BattleStart) as BattleStartEvent | undefined;

		if (battleEffect) {
			player.eventLog.push({
				type: 'combat',
				day: currentDay,
				message: `Event: ${dialog.title} - ${choice.label}`,
			});
			onBattle(battleEffect.enemyName, battleEffect.enemyType, battleEffect.enemyLevel);
			return;
		}

		// Emit non-battle effects via callback
		if (choice.effects && choice.effects.length > 0) {
			onEffects(choice.effects);
		}

		// Log the event
		player.eventLog.push({
			type: 'world',
			day: currentDay,
			message: `Event: ${dialog.title} - ${choice.label}`,
		});

		resultMessage = choice.label;
		if (!choice.effects || choice.effects.length === 0) {
			onClose();
		}
	}
</script>

<div class="absolute inset-0 flex items-center justify-center" style="background: rgba(20, 10, 5, 0.9);">
	<div class="flex h-[380px] w-[540px] flex-col rounded-lg border-4 font-sans" style="background: linear-gradient(to bottom, #e8d4b8, #d4bf9f); border-color: #6b4f3a; box-shadow: 0 8px 16px rgba(0,0,0,0.7), inset 0 2px 0 rgba(255,255,255,0.3);">
		<!-- Title -->
		<div class="border-b px-5 py-3" style="border-color: #8b6f47;">
			<h2 class="text-lg font-black" style="color: #8b6f3a; text-shadow: 0 1px 2px rgba(255,255,255,0.5);">{dialog.title}</h2>
		</div>

		<!-- Description -->
		<div class="border-b px-5 py-3" style="border-color: #8b6f47;">
			<p class="text-sm leading-relaxed" style="color: #5a4a3a;">{dialog.description}</p>
		</div>

		<!-- Choices / result (fixed area) -->
		<div class="flex flex-1 flex-col justify-center gap-2 p-4">
			{#if !resultMessage}
				{#each dialog.choices as choice, i}
					<button
						onclick={() => choose(i)}
						class="rounded border-2 px-4 py-3 text-left text-sm font-bold transition"
						style="background: linear-gradient(to bottom, #c8b89f, #b8a88f); border-color: #8b6f47; color: #3d2817;"
						onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c8b89f)'}
						onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b8a88f)'}
					>{choice.label}</button>
				{/each}
			{:else}
				<div class="mb-3 rounded border px-4 py-3 text-sm" style="background: linear-gradient(to bottom, #b8a890, #a89880); border-color: #6b4f3a; color: #3d2817;">{resultMessage}</div>
				<button
					onclick={onClose}
					class="rounded border-2 px-4 py-3 text-sm font-bold transition"
					style="background: linear-gradient(to bottom, #c8b89f, #b8a88f); border-color: #8b6f47; color: #3d2817;"
					onmouseover={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #d8c8af, #c8b89f)'}
					onmouseout={e => e.currentTarget.style.background = 'linear-gradient(to bottom, #c8b89f, #b8a88f)'}
				>Continue</button>
			{/if}
		</div>
	</div>
</div>

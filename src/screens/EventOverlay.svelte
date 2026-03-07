<script lang="ts">
	import type {PlayerState} from '../game/state';
	import type {ShowDialogEvent, GameEvent, BattleStartEvent} from '../game/event-types';
	import {EventTag} from '../game/event-types';
	import {color, panelStyle, dividerStyle, accentHeadingStyle, bodyStyle, btnProps, messageStyle} from '../ui/theme';

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

<div class="absolute inset-0 flex items-center justify-center" style="background: {color.backdropMedium};">
	<div class="flex h-[380px] w-[540px] flex-col rounded-lg border-4 font-sans" style={panelStyle()}>
		<!-- Title -->
		<div class="border-b px-5 py-3" style={dividerStyle}>
			<h2 class="text-lg font-black" style={accentHeadingStyle}>{dialog.title}</h2>
		</div>

		<!-- Description -->
		<div class="border-b px-5 py-3" style={dividerStyle}>
			<p class="text-sm leading-relaxed" style={bodyStyle}>{dialog.description}</p>
		</div>

		<!-- Choices / result (fixed area) -->
		<div class="flex flex-1 flex-col justify-center gap-2 p-4">
			{#if !resultMessage}
				{#each dialog.choices as choice, i}
					<button
						onclick={() => choose(i)}
						class="rounded border-2 px-4 py-3 text-left text-sm font-bold transition"
						{...btnProps('secondary')}
					>{choice.label}</button>
				{/each}
			{:else}
				<div class="mb-3 rounded border px-4 py-3 text-sm" style={messageStyle}>{resultMessage}</div>
				<button
					onclick={onClose}
					class="rounded border-2 px-4 py-3 text-sm font-bold transition"
					{...btnProps('secondary')}
				>Continue</button>
			{/if}
		</div>
	</div>
</div>
